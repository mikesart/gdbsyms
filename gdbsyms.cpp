#define _GNU_SOURCE 1
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "miniz.h"

#include "threadpool.h"

#define USE_NEW_METHOD 1
#define USE_ART 0
#define USE_CP_FIND_FIRST_COMPONENT 1

#if USE_ART && !USE_NEW_METHOD
#include "libart/src/art.h"
#endif

#define NSECS_PER_MSEC 1000000LL

#define ATTRIBUTE_PRINTF( _x, _y ) __attribute__( ( __format__( __printf__, _x, _y ) ) )

#define FNV1_32A_INIT   0x811c9dc5
extern "C" uint32_t fnv_32a_buf(const void *buf, size_t len, uint32_t hval);

ThreadPool g_threadpool( 2 );

//$ TODO mikesart:
// - std::merge or std::inplace_merge

class MappedFile
{
public:
    void open( const char *filename );
    void close();

public:
    std::string m_filename;
    size_t m_map_size = 0;
    void *m_map = nullptr;
    char *m_data = nullptr;
    char *m_data_end = nullptr;
};

class symbolinfo_t
{
public:
    void add_symbols( const char *filename );

public:
    // Data read from file
    MappedFile m_mfile;

    // Symbols read from file (pointers to map file)
    std::vector< const char * > m_symbol_names;

    // Symbol components read from file
    size_t m_name_component_count = 0;
    std::unordered_map< uint32_t, std::vector< uint32_t > > m_name_components;
};

typedef uint32_t offset_type;

enum case_sensitivity
{
    case_sensitive_on,
    case_sensitive_off
} case_sensitivity;

struct name_component
{
    offset_type name_offset;
    offset_type idx;
};

class lookup_name_info final
{
public:
    bool completion_mode() const { return true; }

public:
    std::string lookup_name;
};

#if USE_NEW_METHOD
struct symentry_t
{
    uint32_t idx;
    // This symbol offset into idxlist[0] symbol
    uint16_t offset;
    // This symbol length
    uint16_t length;
    // List of symbol indices containing this symbol
    std::vector< uint32_t > idxlist;
};
#endif

struct mapped_index_base
{
public:
    uint64_t time_add_components;
    uint64_t time_sort;
    size_t count_symbols;
    size_t count_vec_entries = 0;
    static uint64_t total_time_add_components;
    static uint64_t total_time_sort;
    static size_t total_count_symbols;
    static size_t total_vec_entries;

#if USE_NEW_METHOD
    std::vector< symentry_t > symentries;
#elif USE_ART
    art_tree arttree;
    ~mapped_index_base()
    {
        art_tree_destroy(&arttree);
    }
#else
    std::vector< name_component > name_components;
#endif

    enum case_sensitivity name_components_casing = case_sensitive_on;

public:
    virtual size_t symbol_name_count() const = 0;
    virtual const char *symbol_name_at( offset_type idx ) const = 0;

    void build_name_components();

#if !USE_ART && !USE_NEW_METHOD
    /* Returns the lower (inclusive) and upper (exclusive) bounds of the
     possible matches for LN_NO_PARAMS in the name component
     vector.  */
    std::pair< std::vector< name_component >::const_iterator,
               std::vector< name_component >::const_iterator >
    find_name_components_bounds( const lookup_name_info &ln_no_params ) const;
#endif

    symbolinfo_t *m_syminfo;
};

struct mapped_index final : public mapped_index_base
{
    const char *symbol_name_at( offset_type idx ) const override
    {
        return m_syminfo->m_symbol_names[ idx ];
    }

    size_t symbol_name_count() const override
    {
        return m_syminfo->m_symbol_names.size();
    }
};

uint64_t mapped_index_base::total_time_add_components = 0;
uint64_t mapped_index_base::total_time_sort = 0;
uint64_t mapped_index_base::total_count_symbols = 0;
uint64_t mapped_index_base::total_vec_entries = 0;

unsigned int cp_find_first_component( const char *name );

static void die( const char *fmt, ... ) ATTRIBUTE_PRINTF( 1, 2 );
static void die( const char *fmt, ... )
{
    va_list args;

    va_start( args, fmt );
    vprintf( fmt, args );
    va_end( args );

    exit( -1 );
}

inline uint64_t gettime_u64( void )
{
    struct timespec ts;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ( ( uint64_t )ts.tv_sec * 1000000000LL ) + ts.tv_nsec;
}

inline double time_to_ms( uint64_t time )
{
    return time * ( 1.0 / NSECS_PER_MSEC );
}

inline bool util_file_exists( const char *filename )
{
    return ( access( filename, F_OK ) != -1 );
}

const char *util_get_basename( const char *filename )
{
    const char *basename = strrchr( filename, '/' );

    return basename ? ( basename + 1 ) : filename;
}

static std::string unzip_first_file( const char *zipfile )
{
    std::string ret;
    mz_zip_archive zip_archive;

    memset( &zip_archive, 0, sizeof( zip_archive ) );

    if ( mz_zip_reader_init_file( &zip_archive, zipfile, 0 ) )
    {
        mz_uint fileCount = mz_zip_reader_get_num_files( &zip_archive );

        if ( fileCount )
        {
            mz_zip_archive_file_stat file_stat;

            if ( mz_zip_reader_file_stat( &zip_archive, 0, &file_stat ) )
            {
                for ( mz_uint i = 0; i < fileCount; i++ )
                {
                    if ( !mz_zip_reader_file_stat( &zip_archive, i, &file_stat ) )
                        continue;

                    if ( file_stat.m_is_directory )
                        continue;

                    const char *dot = strrchr( zipfile, '.' );
                    size_t len = dot ? ( dot - zipfile ) : strlen( zipfile );

                    ret = std::string( zipfile, len ) + ".txt";

                    if ( mz_zip_reader_extract_to_file( &zip_archive, i, ret.c_str(), 0 ) )
                        break;

                    ret.clear();
                }
            }
        }

        mz_zip_reader_end( &zip_archive );
    }

    return ret;
}


void MappedFile::open( const char *filename )
{
    struct stat statbuf;
    int fd = ::open( filename, O_RDONLY );

    if ( fd < 0 )
        die( "ERROR: Open(%s) failed: %d\n", filename, errno );

    if ( fstat( fd, &statbuf ) < 0 )
        die( "ERROR: fstat failed: %d\n", errno );

    m_map_size = statbuf.st_size;
    m_map = mmap( NULL, m_map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0 );
    if ( m_map == MAP_FAILED )
        die( "ERROR: mmap failed: %d\n", errno );

    m_data = ( char * )m_map;
    m_data_end = m_data + m_map_size;

    ::close( fd );

    m_filename = util_get_basename( filename );
}

void MappedFile::close()
{
    if ( m_map )
    {
        munmap( m_map, m_map_size );

        m_data = NULL;
        m_data_end = NULL;
        m_map = NULL;
        m_map_size = 0;
    }
}

void symbolinfo_t::add_symbols( const char *filename )
{
    if ( !util_file_exists( filename ) )
        die( "ERROR: %s does not exist...\n", filename );

    printf( "  Reading %s...", util_get_basename( filename ) );

    std::string filename_unzipped;
    static const char *ext = strrchr( filename, '.' );
    if ( ext && !strcasecmp( ext, ".zip" ) )
    {
        uint64_t t0 = gettime_u64();

        filename_unzipped = unzip_first_file( filename );
        if ( filename_unzipped.empty() )
            die( "ERROR: unzipping %s...\n", filename );

        printf( " unzip:%7.2f ms",
                time_to_ms( gettime_u64() - t0 ) );

        filename = filename_unzipped.c_str();
    }

    uint64_t t0 = gettime_u64();

    m_mfile.open( filename );

    char *sym = m_mfile.m_data;
    char *m_data_end = m_mfile.m_data_end;

    // Parse lines like:
    //   'std::__1::basic_ios<char, std::__1::char_traits<char> >::setstate' [ 0 5 10 57]\n
    while ( sym < m_data_end )
    {
        // Search for ending \n
        char *lf = strchr( sym, '\n' );
        char *sym1 = lf - 1;
        char *offsets = lf;

        // Search for trailing quote char
        while ( *sym1 != '\'' )
        {
            // Check if this is start of offsets
            if ( *sym1 == '[' )
                offsets = sym1 + 1;
            sym1--;
        }

        // Terminate symbol name, and add to list
        *sym1 = 0;
        m_symbol_names.push_back( sym + 1 );

        // Parse symbol component offsets: " 0 5 10 57]"
        size_t idx = m_symbol_names.size() - 1;
        while ( offsets < lf )
        {
            char *endptr;
            unsigned int val = strtoul( offsets, &endptr, 10 );

            if ( offsets == endptr )
                break;

            m_name_components[ idx ].push_back( val );
            m_name_component_count++;

            offsets = endptr;
        }

        // Next line
        sym = lf + 1;
    }

    printf( " symbols:%7lu name_components:%7lu time:%7.2f ms\n",
            m_name_components.size(), m_name_component_count,
            time_to_ms( gettime_u64() - t0 ) );
}

#if USE_NEW_METHOD

bool is_tok( char ch )
{
    return ( ch == ':' || ch == ',' || ch == ' ' );
}

const char *tok_find_end( const char *name )
{
    for (;;)
    {
        char ch = name[ 0 ];

        if ( !ch || is_tok( ch ) )
            return name;

        name++;
    }
}

const char *tok_skip_space( const char *name )
{
    while ( is_tok( *name ) )
        name++;

    return name;
}

#endif

void mapped_index_base::build_name_components()
{
    uint64_t t0 = gettime_u64();
    size_t count = symbol_name_count();

#if USE_NEW_METHOD

    std::unordered_map< uint32_t, uint32_t > symmap;

    for ( uint32_t idx = 0; idx < count; idx++ )
    {
        const char *name = symbol_name_at( idx );
        const char *sym = name;

        while ( *sym )
        {
            const char *symend = tok_find_end( sym );
            uint32_t hashval = fnv_32a_buf( sym, symend - sym, FNV1_32A_INIT );

            if ( symmap.find( hashval ) == symmap.end() )
            {
                symentry_t symentry;

                symentry.idx = idx;
                symentry.offset = sym - name;
                symentry.length = symend - sym;

                symmap[ hashval ] = symentries.size();
                symentries.push_back( symentry );
            }
            else
            {
                size_t index = symmap[ hashval ];
                symentry_t &symentry = symentries[ index ];
                std::vector< uint32_t > &idxlist = symentry.idxlist;

                if ( ( symentry.idx != idx ) && ( idxlist.empty() || ( idxlist.back() != idx ) ) )
                {
                    idxlist.push_back( idx );
                    count_vec_entries++;
                }
            }

            sym = tok_skip_space( symend );
        }
    }

    count_symbols = symentries.size();

    uint64_t t1 = gettime_u64();

    auto *name_cmp = ( this->name_components_casing == case_sensitive_on ) ?
                         strncmp : strncasecmp;

    auto name_comp_compare = [&]( const symentry_t &left,
                                  const symentry_t &right )
    {
        const char *left_qualified = this->symbol_name_at( left.idx );
        const char *right_qualified = this->symbol_name_at( right.idx );

        const char *left_name = left_qualified + left.offset;
        const char *right_name = right_qualified + right.offset;

        uint32_t len = std::min< uint32_t >( left.length, right.length );
        int ret = name_cmp( left_name, right_name, len );
        if ( ret == 0 && ( left.length != right.length ) )
        {
            ret = left.length - right.length;
        }

        return ret < 0;
    };

    std::sort( symentries.begin(), symentries.end(), name_comp_compare );

#if 0
    for ( symentry_t &symentry : symentries )
    {
        const char *qualified = this->symbol_name_at( symentry.idx );
        const char *name = qualified + symentry.offset;

        printf( "%.*s\n", ( int )symentry.length, name );
    }
#endif

#elif USE_ART

    art_tree_init( &arttree );

    for ( uint32_t idx = 0; idx < count; idx++ )
    {
        const char *name = symbol_name_at( idx );
        size_t name_len = strlen( name );

        art_insert( &arttree, ( unsigned char * )name, name_len, NULL );
        count_symbols++;
    }

    uint64_t t1 = gettime_u64();

#else

    for ( uint32_t idx = 0; idx < count; idx++ )
    {
#if !USE_CP_FIND_FIRST_COMPONENT
        std::vector< uint32_t > &components = m_syminfo->m_name_components[ idx ];

        for ( uint32_t previous_len : components )
        {
            name_components.push_back( { previous_len, idx } );
        }

#else
        const char *name = symbol_name_at( idx );

        /* Add each name component to the name component table.  */
        unsigned int previous_len = 0;

        for ( unsigned int current_len = cp_find_first_component( name );
              name[ current_len ] != '\0';
              current_len += cp_find_first_component( name + current_len ) )
        {
            name_components.push_back( { previous_len, idx } );

            /* Skip the '::'.  */
            current_len += 2;
            previous_len = current_len;
        }

        name_components.push_back( { previous_len, idx } );

#endif
    }

    count_symbols = name_components.size();

    uint64_t t1 = gettime_u64();

    auto *name_cmp = ( this->name_components_casing == case_sensitive_on ) ?
                         strcmp : strcasecmp;
    auto name_comp_compare = [&]( const name_component &left,
                                  const name_component &right )
    {
        const char *left_qualified = this->symbol_name_at( left.idx );
        const char *right_qualified = this->symbol_name_at( right.idx );

        const char *left_name = left_qualified + left.name_offset;
        const char *right_name = right_qualified + right.name_offset;

        return name_cmp( left_name, right_name ) < 0;
    };

    std::sort( name_components.begin(), name_components.end(),
               name_comp_compare );

#endif // !USE_ART

    uint64_t t2 = gettime_u64();

    time_add_components = t1 - t0;
    time_sort = t2 - t1;

    total_time_add_components += time_add_components;
    total_time_sort += time_sort;
    total_count_symbols += count_symbols;
    total_vec_entries += count_vec_entries;
}

#if !USE_ART && !USE_NEW_METHOD
/* Starting from a search name, return the string that finds the upper
   bound of all strings that start with SEARCH_NAME in a sorted name
   list.  Returns the empty string to indicate that the upper bound is
   the end of the list.  */

static std::string
make_sort_after_prefix_name( const char *search_name )
{
    /* When looking to complete "func", we find the upper bound of all
     symbols that start with "func" by looking for where we'd insert
     the closest string that would follow "func" in lexicographical
     order.  Usually, that's "func"-with-last-character-incremented,
     i.e. "fund".  Mind non-ASCII characters, though.  Usually those
     will be UTF-8 multi-byte sequences, but we can't be certain.
     Especially mind the 0xff character, which is a valid character in
     non-UTF-8 source character sets (e.g. Latin1 'ÿ'), and we can't
     rule out compilers allowing it in identifiers.  Note that
     conveniently, strcmp/strcasecmp are specified to compare
     characters interpreted as unsigned char.  So what we do is treat
     the whole string as a base 256 number composed of a sequence of
     base 256 "digits" and add 1 to it.  I.e., adding 1 to 0xff wraps
     to 0, and carries 1 to the following more-significant position.
     If the very first character in SEARCH_NAME ends up incremented
     and carries/overflows, then the upper bound is the end of the
     list.  The string after the empty string is also the empty
     string.

     Some examples of this operation:

       SEARCH_NAME  => "+1" RESULT

       "abc"              => "abd"
       "ab\xff"           => "ac"
       "\xff" "a" "\xff"  => "\xff" "b"
       "\xff"             => ""
       "\xff\xff"         => ""
       ""                 => ""

     Then, with these symbols for example:

      func
      func1
      fund

     completing "func" looks for symbols between "func" and
     "func"-with-last-character-incremented, i.e. "fund" (exclusive),
     which finds "func" and "func1", but not "fund".

     And with:

      funcÿ     (Latin1 'ÿ' [0xff])
      funcÿ1
      fund

     completing "funcÿ" looks for symbols between "funcÿ" and "fund"
     (exclusive), which finds "funcÿ" and "funcÿ1", but not "fund".

     And with:

      ÿÿ        (Latin1 'ÿ' [0xff])
      ÿÿ1

     completing "ÿ" or "ÿÿ" looks for symbols between between "ÿÿ" and
     the end of the list.
  */
    std::string after = search_name;
    while ( !after.empty() && ( unsigned char )after.back() == 0xff )
        after.pop_back();
    if ( !after.empty() )
        after.back() = ( unsigned char )after.back() + 1;
    return after;
}
#endif

#if !USE_ART && !USE_NEW_METHOD
std::pair< std::vector< name_component >::const_iterator,
           std::vector< name_component >::const_iterator >
mapped_index_base::find_name_components_bounds( const lookup_name_info &lookup_name_without_params ) const
{
    auto *name_cmp = this->name_components_casing == case_sensitive_on ? strcmp : strcasecmp;

    const char *cplus = lookup_name_without_params.lookup_name.c_str();

    /* Comparison function object for lower_bound that matches against a
       given symbol name.  */
    auto lookup_compare_lower = [&]( const name_component &elem,
                                     const char *name )
    {
        const char *elem_qualified = this->symbol_name_at( elem.idx );
        const char *elem_name = elem_qualified + elem.name_offset;
        return name_cmp( elem_name, name ) < 0;
    };

    /* Comparison function object for upper_bound that matches against a
       given symbol name.  */
    auto lookup_compare_upper = [&]( const char *name,
                                     const name_component &elem )
    {
        const char *elem_qualified = this->symbol_name_at( elem.idx );
        const char *elem_name = elem_qualified + elem.name_offset;
        return name_cmp( name, elem_name ) < 0;
    };

    auto begin = this->name_components.begin();
    auto end = this->name_components.end();

    /* Find the lower bound.  */
    auto lower = [&]()
    {
        if ( lookup_name_without_params.completion_mode() && cplus[ 0 ] == '\0' )
            return begin;
        else
            return std::lower_bound( begin, end, cplus, lookup_compare_lower );
    }();

    /* Find the upper bound.  */
    auto upper = [&]()
    {
        if ( lookup_name_without_params.completion_mode() )
        {
            /* In completion mode, we want UPPER to point past all
               symbols names that have the same prefix.  I.e., with
               these symbols, and completing "func":

                function        << lower bound
                function1
                other_function  << upper bound

               We find the upper bound by looking for the insertion
               point of "func"-with-last-character-incremented,
               i.e. "fund".  */
            std::string after = make_sort_after_prefix_name( cplus );
            if ( after.empty() )
                return end;
            return std::lower_bound( lower, end, after.c_str(),
                                     lookup_compare_lower );
        }
        else
        {
            return std::upper_bound( lower, end, cplus, lookup_compare_upper );
        }
    }();

    return { lower, upper };
}
#endif

#if !USE_ART && !USE_NEW_METHOD
static void
dw2_expand_symtabs_matching_symbol( mapped_index_base &index,
                                    const lookup_name_info &lookup_name_in )
{
    lookup_name_info lookup_name_without_params = lookup_name_in;

#if 0
    /* Build the symbol name component sorted vector, if we haven't
       yet.  */
    index.build_name_components();
#endif

    auto bounds = index.find_name_components_bounds( lookup_name_without_params );

    /* Now for each symbol name in range, check to see if we have a name
       match, and if so, call the MATCH_CALLBACK callback.  */

    /* The same symbol may appear more than once in the range though.
       E.g., if we're looking for symbols that complete "w", and we have
       a symbol named "w1::w2", we'll find the two name components for
       that same symbol in the range.  To be sure we only call the
       callback once per symbol, we first collect the symbol name
       indexes that matched in a temporary vector and ignore
       duplicates.  */
    std::vector< offset_type > matches;
    matches.reserve( std::distance( bounds.first, bounds.second ) );

    for ( ; bounds.first != bounds.second; ++bounds.first )
    {
        const char *qualified = index.symbol_name_at( bounds.first->idx );

#if 0
        if ( !lookup_name_matcher.matches( qualified ) ||
             ( symbol_matcher != NULL && !symbol_matcher( qualified ) ) )
        {
            continue;
        }
#endif

        printf( "match: %s\n", qualified );

        matches.push_back( bounds.first->idx );
    }

    printf( "matches: %lu\n", matches.size() );

    std::sort( matches.begin(), matches.end() );

#if 0
    /* Finally call the callback, once per match.  */
    ULONGEST prev = -1;
    for ( offset_type idx : matches )
    {
        if ( prev != idx )
        {
            match_callback( idx );
            prev = idx;
        }
    }

    /* Above we use a type wider than idx's for 'prev', since 0 and
       (offset_type)-1 are both possible values.  */
    static_assert( sizeof( prev ) > sizeof( offset_type ), "" );
#endif
}
#endif

/* A string representing the start of an operator name.  */

#define CP_OPERATOR_STR "operator"

/* The length of CP_OPERATOR_STR.  */

#define CP_OPERATOR_LEN 8

static uint32_t demangled_name_complaint_count = 0;

static void
demangled_name_complaint( const char *name )
{
    //$ TODO mikesart: printf( "WARNING: unexpected demangled name '%s'", name );
    demangled_name_complaint_count++;
}

static inline int
startswith( const char *string, const char *pattern )
{
    return strncmp( string, pattern, strlen( pattern ) ) == 0;
}

/* Helper function for cp_find_first_component.  Like that function,
   it returns the length of the first component of NAME, but to make
   the recursion easier, it also stops if it reaches an unexpected ')'
   or '>' if the value of PERMISSIVE is nonzero.  */

static unsigned int
cp_find_first_component_aux( const char *name, int permissive )
{
    unsigned int index = 0;
    /* Operator names can show up in unexpected places.  Since these can
       contain parentheses or angle brackets, they can screw up the
       recursion.  But not every string 'operator' is part of an
       operater name: e.g. you could have a variable 'cooperator'.  So
       this variable tells us whether or not we should treat the string
       'operator' as starting an operator.  */
    int operator_possible = 1;

    for ( ;; ++index )
    {
        switch ( name[ index ] )
        {
        case '<':
            /* Template; eat it up.  The calls to cp_first_component
               should only return (I hope!) when they reach the '>'
               terminating the component or a '::' between two
               components.  (Hence the '+ 2'.)  */
            index += 1;
            for ( index += cp_find_first_component_aux( name + index, 1 );
                  name[ index ] != '>';
                  index += cp_find_first_component_aux( name + index, 1 ) )
            {
                if ( name[ index ] != ':' )
                {
                    demangled_name_complaint( name );
                    return strlen( name );
                }
                index += 2;
            }
            operator_possible = 1;
            break;
        case '(':
            /* Similar comment as to '<'.  */
            index += 1;
            for ( index += cp_find_first_component_aux( name + index, 1 );
                  name[ index ] != ')';
                  index += cp_find_first_component_aux( name + index, 1 ) )
            {
                if ( name[ index ] != ':' )
                {
                    demangled_name_complaint( name );
                    return strlen( name );
                }
                index += 2;
            }
            operator_possible = 1;
            break;
        case '>':
        case ')':
            if ( permissive )
                return index;
            else
            {
                demangled_name_complaint( name );
                return strlen( name );
            }
        case '\0':
            return index;
        case ':':
            /* ':' marks a component iff the next character is also a ':'.
                Otherwise it is probably malformed input.  */
            if ( name[ index + 1 ] == ':' )
                return index;
            break;
        case 'o':
            /* Operator names can screw up the recursion.  */
            if ( operator_possible && startswith( name + index, CP_OPERATOR_STR ) )
            {
                index += CP_OPERATOR_LEN;
                while ( isspace( name[ index ] ) )
                    ++index;
                switch ( name[ index ] )
                {
                case '\0':
                    return index;
                /* Skip over one less than the appropriate number of
                   characters: the for loop will skip over the last
                   one.  */
                case '<':
                    if ( name[ index + 1 ] == '<' )
                        index += 1;
                    else
                        index += 0;
                    break;
                case '>':
                case '-':
                    if ( name[ index + 1 ] == '>' )
                        index += 1;
                    else
                        index += 0;
                    break;
                case '(':
                    index += 1;
                    break;
                default:
                    index += 0;
                    break;
                }
            }
            operator_possible = 0;
            break;
        case ' ':
        case ',':
        case '.':
        case '&':
        case '*':
            /* NOTE: carlton/2003-04-18: I'm not sure what the precise
               set of relevant characters are here: it's necessary to
               include any character that can show up before 'operator'
               in a demangled name, and it's safe to include any
               character that can't be part of an identifier's name.  */
            operator_possible = 1;
            break;
        default:
            operator_possible = 0;
            break;
        }
    }
}

unsigned int
cp_find_first_component( const char *name )
{
    return cp_find_first_component_aux( name, 0 );
}

#if 0
int mult1( int a, int b )
{
    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );

    return a * b;
}

void mult2( int &result, int a, int b )
{
    result = a * b;
}
#endif

int main( int argc, char *argv[] )
{
#if 0
    std::future< int > future1 = g_threadpool.submit_job( "job0", mult1, 2, 3 );
    auto future2 = g_threadpool.submit_job( "job1", mult1, 2, 4 );

    int result3 = 0;
    auto future3 = g_threadpool.submit_job( "job2", mult2, std::ref( result3 ), 2, 6 );

    int result1 = future1.get();
    int result2 = future2.get();

    future3.get();

    printf( "result1:%d result2:%d result3:%d\n", result1, result2, result3 );
    return 0;
#endif

    if ( argc <= 1 )
        die( "ERROR: No files specified.\n" );

    std::vector< symbolinfo_t > syminfos( argc - 1 );
    std::vector< mapped_index > mindexes( argc - 1 );

    for ( size_t i = 0; i < syminfos.size(); i++ )
    {
        syminfos[ i ].add_symbols( argv[ i + 1 ] );
    }

    printf( "\n" );

    for ( size_t i = 0; i < syminfos.size(); i++ )
    {
        mapped_index &mindex = mindexes[ i ];
        const std::string &filename = syminfos[ i ].m_mfile.m_filename;

        mindex.m_syminfo = &syminfos[ i ];

        mindex.build_name_components();

#if 0
        lookup_name_info lookup_name;
        lookup_name.lookup_name = "internal::V";
        dw2_expand_symtabs_matching_symbol( mindex, lookup_name );
#endif

        printf( "  build_name_components %s syms:%7lu vecentries:%8lu add_components:%7.2fms sort:%7.2fms\n",
                filename.c_str(),
                mindex.count_symbols,
                mindex.count_vec_entries,
                time_to_ms( mindex.time_add_components ),
                time_to_ms( mindex.time_sort ) );
    }

    printf( "\nTotals syms:%lu vec_entries:%lu add_components:%.2fms sort:%.2fms total:%.2f\n",
            mapped_index::total_count_symbols,
            mapped_index::total_vec_entries,
            time_to_ms( mapped_index::total_time_add_components ),
            time_to_ms( mapped_index::total_time_sort ),
            time_to_ms( mapped_index::total_time_add_components + mapped_index::total_time_sort ) );

    printf( "\ndemangled_name_complaints: %u\n", demangled_name_complaint_count );

    return 0;
}
