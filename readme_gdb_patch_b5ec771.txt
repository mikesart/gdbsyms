commit b5ec771e60c1a0863e51eb491c85c674097e9e13
Author: Pedro Alves <palves@redhat.com>
Date:   Wed Nov 8 14:22:32 2017 +0000

    Introduce lookup_name_info and generalize Ada's FULL/WILD name matching
    
    Summary:
     - This is preparation for supporting wild name matching on C++ too.
     - This is also preparation for TAB-completion fixes.
     - Makes symbol name matching (think strcmp_iw) be based on a per-language method.
     - Merges completion and non-completion name comparison (think
       language_ops::la_get_symbol_name_cmp generalized).
     - Avoid re-hashing lookup name multiple times
     - Centralizes preparing a name for lookup (Ada name encoding / C++ Demangling),
       both completion and non-completion.
     - Fixes Ada latent bug with verbatim name matches in expressions
     - Makes ada-lang.c use common|symtab.c completion code a bit more.
    
    Ada's wild matching basically means that
    
     "(gdb) break foo"
    
    will find all methods named "foo" in all packages.  Translating to
    C++, it's roughly the same as saying that "break klass::method" sets
    breakpoints on all "klass::method" methods of all classes, no matter
    the namespace.  A following patch will teach GDB about fullname vs
    wild matching for C++ too.  This patch is preparatory work to get
    there.
    
    Another idea here is to do symbol name matching based on the symbol
    language's algorithm.  I.e., avoid dependency on current language set.
    
    This allows for example doing
    
      (gdb) b foo::bar< int > (<tab>
    
    and having gdb name match the C++ symbols correctly even if the
    current language is C or Assembly (or Rust, or Ada, or ...), which can
    easily happen if you step into an Assembly/C runtime library frame.
    
    By encapsulating all the information related to a lookup name in a
    class, we can also cache hash computation for a given language in the
    lookup name object, to avoid recomputing it over and over.
    
    Similarly, because we don't really know upfront which languages the
    lookup name will be matched against, for each language we store the
    lookup name transformed into a search name.  E.g., for C++, that means
    demangling the name.  But for Ada, it means encoding the name.  This
    actually forces us to centralize all the different lookup name
    encoding in a central place, resulting in clearer code, IMO.  See
    e.g., the new ada_lookup_name_info class.
    
    The lookup name -> symbol search name computation is also done only
    once per language.
    
    The old language->la_get_symbol_name_cmp / symbol_name_cmp_ftype are
    generalized to work with both completion, and normal symbol look up.
    
    At some point early on, I had separate completion vs non-completion
    language vector entry points, but a single method ends up being better
    IMO for simplifying things -- the more we merge the completion /
    non-completion name lookup code paths, the less changes for bugs
    causing completion vs normal lookup finding different symbols.
    
    The ada-lex.l change is necessary because when doing
    
      (gdb) p <UpperCase>
    
    then the name that is passed to write_ write_var_or_type ->
    ada_lookup_symbol_list misses the "<>", i.e., it's just "UpperCase",
    and we end up doing a wild match against "UpperCase" lowercased by
    ada_lookup_name_info's constructor.  I.e., "uppercase" wouldn't ever
    match "UpperCase", and the symbol lookup fails.
    
    This wouldn't cause any regression in the testsuite, but I added a new
    test that would pass before the patch and fail after, if it weren't
    for that fix.
    
    This is latent bug that happens to go unnoticed because that
    particular path was inconsistent with the rest of Ada symbol lookup by
    not lowercasing the lookup name.
    
    Ada's symbol_completion_add is deleted, replaced by using common
    code's completion_list_add_name.  To make the latter work for Ada, we
    needed to add a new output parameter, because Ada wants to return back
    a custom completion candidates that are not the symbol name.
    
    With this patch, minimal symbol demangled name hashing is made
    consistent with regular symbol hashing.  I.e., it now goes via the
    language vector's search_name_hash method too, as I had suggested in a
    previous patch.
    
    dw2_expand_symtabs_matching / .gdb_index symbol names were a
    challenge.  The problem is that we have no way to telling what is the
    language of each symbol name found in the index, until we expand the
    corresponding full symbol, which is off course what we're trying to
    avoid.  Language information is simply not considered in the index
    format...  Since the symbol name hashing and comparison routines are
    per-language, we now have a problem.  The patch sorts this out by
    matching each name against all languages.  This is inneficient, and
    indeed slows down completion several times.  E.g., with:
    
     $ cat script.cmd
     set pagination off
     set $count = 0
     while $count < 400
       complete b string_prin
       printf "count = %d\n", $count
       set $count = $count + 1
     end
    
     $ time gdb --batch -q ./gdb-with-index -ex "source script-string_printf.cmd"
    
    I get, before patch (-O2, x86-64):
    
     real    0m1.773s
     user    0m1.737s
     sys     0m0.040s
    
    While after patch (-O2, x86-64):
    
     real    0m9.843s
     user    0m9.482s
     sys     0m0.034s
    
    However, the following patch will optimize this, and will actually
    make this use case faster compared to the "before patch" above:
    
     real    0m1.321s
     user    0m1.285s
     sys     0m0.039s
    
    gdb/ChangeLog:
    2017-11-08   Pedro Alves  <palves@redhat.com>
    
            * ada-lang.c (ada_encode): Rename to ..
            (ada_encode_1): ... this.  Add throw_errors parameter and handle
            it.
            (ada_encode): Reimplement.
            (match_name): Delete, folded into full_name.
            (resolve_subexp): No longer pass the encoded name to
            ada_lookup_symbol_list.
            (should_use_wild_match): Delete.
            (name_match_type_from_name): New.
            (ada_lookup_simple_minsym): Use lookup_name_info and the
            language's symbol_name_matcher_ftype.
            (add_symbols_from_enclosing_procs, ada_add_local_symbols)
            (ada_add_block_renamings): Adjust to use lookup_name_info.
            (ada_lookup_name): New.
            (add_nonlocal_symbols, ada_add_all_symbols)
            (ada_lookup_symbol_list_worker, ada_lookup_symbol_list)
            (ada_iterate_over_symbols): Adjust to use lookup_name_info.
            (ada_name_for_lookup): Delete.
            (ada_lookup_encoded_symbol): Construct a verbatim name.
            (wild_match): Reverse sense of return type.  Use bool.
            (full_match): Reverse sense of return type.  Inline bits of old
            match_name here.
            (ada_add_block_symbols): Adjust to use lookup_name_info.
            (symbol_completion_match): Delete, folded into...
            (ada_lookup_name_info::matches): ... .this new method.
            (symbol_completion_add): Delete.
            (ada_collect_symbol_completion_matches): Add name_match_type
            parameter.  Adjust to use lookup_name_info and
            completion_list_add_name.
            (get_var_value, ada_add_global_exceptions): Adjust to use
            lookup_name_info.
            (ada_get_symbol_name_cmp): Delete.
            (do_wild_match, do_full_match): New functions.
            (ada_lookup_name_info::ada_lookup_name_info): New method.
            (ada_symbol_name_matches, ada_get_symbol_name_matcher): New
            functions.
            (ada_language_defn): Install ada_get_symbol_name_matcher.
            * ada-lex.l (processId): If name starts with '<', copy it
            verbatim.
            * block.c (block_iter_match_step, block_iter_match_first)
            (block_iter_match_next, block_lookup_symbol)
            (block_lookup_symbol_primary, block_find_symbol): Adjust to use
            lookup_name_info.
            * block.h (block_iter_match_first, block_iter_match_next)
            (ALL_BLOCK_SYMBOLS_WITH_NAME): Adjust to use lookup_name_info.
            * c-lang.c (c_language_defn, cplus_language_defn)
            (asm_language_defn, minimal_language_defn): Adjust comments to
            refer to la_get_symbol_name_matcher.
            * completer.c (complete_files_symbols)
            (collect_explicit_location_matches, symbol_completer): Pass a
            symbol_name_match_type down.
            * completer.h (class completion_match, completion_match_result):
            New classes.
            (completion_tracker::reset_completion_match_result): New method.
            (completion_tracker::m_completion_match_result): New field.
            * cp-support.c (make_symbol_overload_list_block): Adjust to use
            lookup_name_info.
            (cp_fq_symbol_name_matches, cp_get_symbol_name_matcher): New
            functions.
            * cp-support.h (cp_get_symbol_name_matcher): New declaration.
            * d-lang.c: Adjust comments to refer to
            la_get_symbol_name_matcher.
            * dictionary.c (dict_vector) <iter_match_first, iter_match_next>:
            Adjust to use lookup_name_info.
            (dict_iter_match_first, dict_iter_match_next)
            (iter_match_first_hashed, iter_match_next_hashed)
            (iter_match_first_linear, iter_match_next_linear): Adjust to work
            with a lookup_name_info.
            * dictionary.h (dict_iter_match_first, dict_iter_match_next):
            Likewise.
            * dwarf2read.c (dw2_lookup_symbol): Adjust to use lookup_name_info.
            (dw2_map_matching_symbols): Adjust to use symbol_name_match_type.
            (gdb_index_symbol_name_matcher): New class.
            (dw2_expand_symtabs_matching) Adjust to use lookup_name_info and
            gdb_index_symbol_name_matcher.  Accept a NULL symbol_matcher.
            * f-lang.c (f_collect_symbol_completion_matches): Adjust to work
            with a symbol_name_match_type.
            (f_language_defn): Adjust comments to refer to
            la_get_symbol_name_matcher.
            * go-lang.c (go_language_defn): Adjust comments to refer to
            la_get_symbol_name_matcher.
            * language.c (default_symbol_name_matcher)
            (language_get_symbol_name_matcher): New functions.
            (unknown_language_defn, auto_language_defn): Adjust comments to
            refer to la_get_symbol_name_matcher.
            * language.h (symbol_name_cmp_ftype): Delete.
            (language_defn) <la_collect_symbol_completion_matches>: Add match
            type parameter.
            <la_get_symbol_name_cmp>: Delete field.
            <la_get_symbol_name_matcher>: New field.
            <la_iterate_over_symbols>: Adjust to use lookup_name_info.
            (default_symbol_name_matcher, language_get_symbol_name_matcher):
            Declare.
            * linespec.c (iterate_over_all_matching_symtabs)
            (iterate_over_file_blocks): Adjust to use lookup_name_info.
            (find_methods): Add language parameter, and use lookup_name_info
            and the language's symbol_name_matcher_ftype.
            (linespec_complete_function): Adjust.
            (lookup_prefix_sym): Use lookup_name_info.
            (add_all_symbol_names_from_pspace): Adjust.
            (find_superclass_methods): Add language parameter and pass it
            down.
            (find_method): Pass symbol language down.
            (find_linespec_symbols): Don't demangle or Ada encode here.
            (search_minsyms_for_name): Add lookup_name_info parameter.
            (add_matching_symbols_to_info): Add name_match_type parameter.
            Use lookup_name_info.
            * m2-lang.c (m2_language_defn): Adjust comments to refer to
            la_get_symbol_name_matcher.
            * minsyms.c: Include <algorithm>.
            (add_minsym_to_demangled_hash_table): Remove table parameter and
            add objfile parameter.  Use search_name_hash, and add language to
            demangled languages vector.
            (struct found_minimal_symbols): New struct.
            (lookup_minimal_symbol_mangled, lookup_minimal_symbol_demangled):
            New functions.
            (lookup_minimal_symbol): Adjust to use them.  Don't canonicalize
            input names here.  Use lookup_name_info instead.  Lookup up
            demangled names once for each language in the demangled names
            vector.
            (iterate_over_minimal_symbols): Use lookup_name_info.  Lookup up
            demangled names once for each language in the demangled names
            vector.
            (build_minimal_symbol_hash_tables): Adjust.
            * minsyms.h (iterate_over_minimal_symbols): Adjust to pass down a
            lookup_name_info.
            * objc-lang.c (objc_language_defn): Adjust comment to refer to
            la_get_symbol_name_matcher.
            * objfiles.h: Include <vector>.
            (objfile_per_bfd_storage) <demangled_hash_languages>: New field.
            * opencl-lang.c (opencl_language_defn): Adjust comment to refer to
            la_get_symbol_name_matcher.
            * p-lang.c (pascal_language_defn): Adjust comment to refer to
            la_get_symbol_name_matcher.
            * psymtab.c (psym_lookup_symbol): Use lookup_name_info.
            (match_partial_symbol): Use symbol_name_match_type,
            lookup_name_info and psymbol_name_matches.
            (lookup_partial_symbol): Use lookup_name_info.
            (map_block): Use symbol_name_match_type and lookup_name_info.
            (psym_map_matching_symbols): Use symbol_name_match_type.
            (psymbol_name_matches): New.
            (recursively_search_psymtabs): Use lookup_name_info and
            psymbol_name_matches.  Rename 'kind' parameter to 'domain'.
            (psym_expand_symtabs_matching): Use lookup_name_info.  Rename
            'kind' parameter to 'domain'.
            * rust-lang.c (rust_language_defn): Adjust comment to refer to
            la_get_symbol_name_matcher.
            * symfile-debug.c (debug_qf_map_matching_symbols)
            (debug_qf_map_matching_symbols): Use symbol_name_match_type.
            (debug_qf_expand_symtabs_matching): Use lookup_name_info.
            * symfile.c (expand_symtabs_matching): Use lookup_name_info.
            * symfile.h (quick_symbol_functions) <map_matching_symbols>:
            Adjust to use symbol_name_match_type.
            <expand_symtabs_matching>: Adjust to use lookup_name_info.
            (expand_symtabs_matching): Adjust to use lookup_name_info.
            * symmisc.c (maintenance_expand_symtabs): Use
            lookup_name_info::match_any ().
            * symtab.c (symbol_matches_search_name): New.
            (eq_symbol_entry): Adjust to use lookup_name_info and the
            language's matcher.
            (demangle_for_lookup_info::demangle_for_lookup_info): New.
            (lookup_name_info::match_any): New.
            (iterate_over_symbols, search_symbols): Use lookup_name_info.
            (compare_symbol_name): Add language, lookup_name_info and
            completion_match_result parameters, and use them.
            (completion_list_add_name): Make extern.  Add language and
            lookup_name_info parameters.  Use them.
            (completion_list_add_symbol, completion_list_add_msymbol)
            (completion_list_objc_symbol): Add lookup_name_info parameters and
            adjust.  Pass down language.
            (completion_list_add_fields): Add lookup_name_info parameters and
            adjust.  Pass down language.
            (add_symtab_completions): Add lookup_name_info parameters and
            adjust.
            (default_collect_symbol_completion_matches_break_on): Add
            name_match_type parameter, and use it.  Use lookup_name_info.
            (default_collect_symbol_completion_matches)
            (collect_symbol_completion_matches): Add name_match_type
            parameter, and pass it down.
            (collect_symbol_completion_matches_type): Adjust.
            (collect_file_symbol_completion_matches): Add name_match_type
            parameter, and use lookup_name_info.
            * symtab.h: Include <string> and "common/gdb_optional.h".
            (enum class symbol_name_match_type): New.
            (class ada_lookup_name_info): New.
            (struct demangle_for_lookup_info): New.
            (class lookup_name_info): New.
            (symbol_name_matcher_ftype): New.
            (SYMBOL_MATCHES_SEARCH_NAME): Use symbol_matches_search_name.
            (symbol_matches_search_name): Declare.
            (MSYMBOL_MATCHES_SEARCH_NAME): Delete.
            (default_collect_symbol_completion_matches)
            (collect_symbol_completion_matches)
            (collect_file_symbol_completion_matches): Add name_match_type
            parameter.
            (iterate_over_symbols): Use lookup_name_info.
            (completion_list_add_name): Declare.
            * utils.c (enum class strncmp_iw_mode): Moved to utils.h.
            (strncmp_iw_with_mode): Now extern.
            * utils.h (enum class strncmp_iw_mode): Moved from utils.c.
            (strncmp_iw_with_mode): Declare.
    
    gdb/testsuite/ChangeLog:
    2017-11-08   Pedro Alves  <palves@redhat.com>
    
            * gdb.ada/complete.exp (p <Exported_Capitalized>): New test.
            (p Exported_Capitalized): New test.
            (p exported_capitalized): New test.

diff --git a/gdb/ChangeLog b/gdb/ChangeLog
index f793c21..06ababc 100644
--- a/gdb/ChangeLog
+++ b/gdb/ChangeLog
@@ -1,3 +1,207 @@
+2017-11-08   Pedro Alves  <palves@redhat.com>
+
+	* ada-lang.c (ada_encode): Rename to ..
+	(ada_encode_1): ... this.  Add throw_errors parameter and handle
+	it.
+	(ada_encode): Reimplement.
+	(match_name): Delete, folded into full_name.
+	(resolve_subexp): No longer pass the encoded name to
+	ada_lookup_symbol_list.
+	(should_use_wild_match): Delete.
+	(name_match_type_from_name): New.
+	(ada_lookup_simple_minsym): Use lookup_name_info and the
+	language's symbol_name_matcher_ftype.
+	(add_symbols_from_enclosing_procs, ada_add_local_symbols)
+	(ada_add_block_renamings): Adjust to use lookup_name_info.
+	(ada_lookup_name): New.
+	(add_nonlocal_symbols, ada_add_all_symbols)
+	(ada_lookup_symbol_list_worker, ada_lookup_symbol_list)
+	(ada_iterate_over_symbols): Adjust to use lookup_name_info.
+	(ada_name_for_lookup): Delete.
+	(ada_lookup_encoded_symbol): Construct a verbatim name.
+	(wild_match): Reverse sense of return type.  Use bool.
+	(full_match): Reverse sense of return type.  Inline bits of old
+	match_name here.
+	(ada_add_block_symbols): Adjust to use lookup_name_info.
+	(symbol_completion_match): Delete, folded into...
+	(ada_lookup_name_info::matches): ... .this new method.
+	(symbol_completion_add): Delete.
+	(ada_collect_symbol_completion_matches): Add name_match_type
+	parameter.  Adjust to use lookup_name_info and
+	completion_list_add_name.
+	(get_var_value, ada_add_global_exceptions): Adjust to use
+	lookup_name_info.
+	(ada_get_symbol_name_cmp): Delete.
+	(do_wild_match, do_full_match): New functions.
+	(ada_lookup_name_info::ada_lookup_name_info): New method.
+	(ada_symbol_name_matches, ada_get_symbol_name_matcher): New
+	functions.
+	(ada_language_defn): Install ada_get_symbol_name_matcher.
+	* ada-lex.l (processId): If name starts with '<', copy it
+	verbatim.
+	* block.c (block_iter_match_step, block_iter_match_first)
+	(block_iter_match_next, block_lookup_symbol)
+	(block_lookup_symbol_primary, block_find_symbol): Adjust to use
+	lookup_name_info.
+	* block.h (block_iter_match_first, block_iter_match_next)
+	(ALL_BLOCK_SYMBOLS_WITH_NAME): Adjust to use lookup_name_info.
+	* c-lang.c (c_language_defn, cplus_language_defn)
+	(asm_language_defn, minimal_language_defn): Adjust comments to
+	refer to la_get_symbol_name_matcher.
+	* completer.c (complete_files_symbols)
+	(collect_explicit_location_matches, symbol_completer): Pass a
+	symbol_name_match_type down.
+	* completer.h (class completion_match, completion_match_result):
+	New classes.
+	(completion_tracker::reset_completion_match_result): New method.
+	(completion_tracker::m_completion_match_result): New field.
+	* cp-support.c (make_symbol_overload_list_block): Adjust to use
+	lookup_name_info.
+	(cp_fq_symbol_name_matches, cp_get_symbol_name_matcher): New
+	functions.
+	* cp-support.h (cp_get_symbol_name_matcher): New declaration.
+	* d-lang.c: Adjust comments to refer to
+	la_get_symbol_name_matcher.
+	* dictionary.c (dict_vector) <iter_match_first, iter_match_next>:
+	Adjust to use lookup_name_info.
+	(dict_iter_match_first, dict_iter_match_next)
+	(iter_match_first_hashed, iter_match_next_hashed)
+	(iter_match_first_linear, iter_match_next_linear): Adjust to work
+	with a lookup_name_info.
+	* dictionary.h (dict_iter_match_first, dict_iter_match_next):
+	Likewise.
+	* dwarf2read.c (dw2_lookup_symbol): Adjust to use lookup_name_info.
+	(dw2_map_matching_symbols): Adjust to use symbol_name_match_type.
+	(gdb_index_symbol_name_matcher): New class.
+	(dw2_expand_symtabs_matching) Adjust to use lookup_name_info and
+	gdb_index_symbol_name_matcher.  Accept a NULL symbol_matcher.
+	* f-lang.c (f_collect_symbol_completion_matches): Adjust to work
+	with a symbol_name_match_type.
+	(f_language_defn): Adjust comments to refer to
+	la_get_symbol_name_matcher.
+	* go-lang.c (go_language_defn): Adjust comments to refer to
+	la_get_symbol_name_matcher.
+	* language.c (default_symbol_name_matcher)
+	(language_get_symbol_name_matcher): New functions.
+	(unknown_language_defn, auto_language_defn): Adjust comments to
+	refer to la_get_symbol_name_matcher.
+	* language.h (symbol_name_cmp_ftype): Delete.
+	(language_defn) <la_collect_symbol_completion_matches>: Add match
+	type parameter.
+	<la_get_symbol_name_cmp>: Delete field.
+	<la_get_symbol_name_matcher>: New field.
+	<la_iterate_over_symbols>: Adjust to use lookup_name_info.
+	(default_symbol_name_matcher, language_get_symbol_name_matcher):
+	Declare.
+	* linespec.c (iterate_over_all_matching_symtabs)
+	(iterate_over_file_blocks): Adjust to use lookup_name_info.
+	(find_methods): Add language parameter, and use lookup_name_info
+	and the language's symbol_name_matcher_ftype.
+	(linespec_complete_function): Adjust.
+	(lookup_prefix_sym): Use lookup_name_info.
+	(add_all_symbol_names_from_pspace): Adjust.
+	(find_superclass_methods): Add language parameter and pass it
+	down.
+	(find_method): Pass symbol language down.
+	(find_linespec_symbols): Don't demangle or Ada encode here.
+	(search_minsyms_for_name): Add lookup_name_info parameter.
+	(add_matching_symbols_to_info): Add name_match_type parameter.
+	Use lookup_name_info.
+	* m2-lang.c (m2_language_defn): Adjust comments to refer to
+	la_get_symbol_name_matcher.
+	* minsyms.c: Include <algorithm>.
+	(add_minsym_to_demangled_hash_table): Remove table parameter and
+	add objfile parameter.  Use search_name_hash, and add language to
+	demangled languages vector.
+	(struct found_minimal_symbols): New struct.
+	(lookup_minimal_symbol_mangled, lookup_minimal_symbol_demangled):
+	New functions.
+	(lookup_minimal_symbol): Adjust to use them.  Don't canonicalize
+	input names here.  Use lookup_name_info instead.  Lookup up
+	demangled names once for each language in the demangled names
+	vector.
+	(iterate_over_minimal_symbols): Use lookup_name_info.  Lookup up
+	demangled names once for each language in the demangled names
+	vector.
+	(build_minimal_symbol_hash_tables): Adjust.
+	* minsyms.h (iterate_over_minimal_symbols): Adjust to pass down a
+	lookup_name_info.
+	* objc-lang.c (objc_language_defn): Adjust comment to refer to
+	la_get_symbol_name_matcher.
+	* objfiles.h: Include <vector>.
+	(objfile_per_bfd_storage) <demangled_hash_languages>: New field.
+	* opencl-lang.c (opencl_language_defn): Adjust comment to refer to
+	la_get_symbol_name_matcher.
+	* p-lang.c (pascal_language_defn): Adjust comment to refer to
+	la_get_symbol_name_matcher.
+	* psymtab.c (psym_lookup_symbol): Use lookup_name_info.
+	(match_partial_symbol): Use symbol_name_match_type,
+	lookup_name_info and psymbol_name_matches.
+	(lookup_partial_symbol): Use lookup_name_info.
+	(map_block): Use symbol_name_match_type and lookup_name_info.
+	(psym_map_matching_symbols): Use symbol_name_match_type.
+	(psymbol_name_matches): New.
+	(recursively_search_psymtabs): Use lookup_name_info and
+	psymbol_name_matches.  Rename 'kind' parameter to 'domain'.
+	(psym_expand_symtabs_matching): Use lookup_name_info.  Rename
+	'kind' parameter to 'domain'.
+	* rust-lang.c (rust_language_defn): Adjust comment to refer to
+	la_get_symbol_name_matcher.
+	* symfile-debug.c (debug_qf_map_matching_symbols)
+	(debug_qf_map_matching_symbols): Use symbol_name_match_type.
+	(debug_qf_expand_symtabs_matching): Use lookup_name_info.
+	* symfile.c (expand_symtabs_matching): Use lookup_name_info.
+	* symfile.h (quick_symbol_functions) <map_matching_symbols>:
+	Adjust to use symbol_name_match_type.
+	<expand_symtabs_matching>: Adjust to use lookup_name_info.
+	(expand_symtabs_matching): Adjust to use lookup_name_info.
+	* symmisc.c (maintenance_expand_symtabs): Use
+	lookup_name_info::match_any ().
+	* symtab.c (symbol_matches_search_name): New.
+	(eq_symbol_entry): Adjust to use lookup_name_info and the
+	language's matcher.
+	(demangle_for_lookup_info::demangle_for_lookup_info): New.
+	(lookup_name_info::match_any): New.
+	(iterate_over_symbols, search_symbols): Use lookup_name_info.
+	(compare_symbol_name): Add language, lookup_name_info and
+	completion_match_result parameters, and use them.
+	(completion_list_add_name): Make extern.  Add language and
+	lookup_name_info parameters.  Use them.
+	(completion_list_add_symbol, completion_list_add_msymbol)
+	(completion_list_objc_symbol): Add lookup_name_info parameters and
+	adjust.  Pass down language.
+	(completion_list_add_fields): Add lookup_name_info parameters and
+	adjust.  Pass down language.
+	(add_symtab_completions): Add lookup_name_info parameters and
+	adjust.
+	(default_collect_symbol_completion_matches_break_on): Add
+	name_match_type parameter, and use it.  Use lookup_name_info.
+	(default_collect_symbol_completion_matches)
+	(collect_symbol_completion_matches): Add name_match_type
+	parameter, and pass it down.
+	(collect_symbol_completion_matches_type): Adjust.
+	(collect_file_symbol_completion_matches): Add name_match_type
+	parameter, and use lookup_name_info.
+	* symtab.h: Include <string> and "common/gdb_optional.h".
+	(enum class symbol_name_match_type): New.
+	(class ada_lookup_name_info): New.
+	(struct demangle_for_lookup_info): New.
+	(class lookup_name_info): New.
+	(symbol_name_matcher_ftype): New.
+	(SYMBOL_MATCHES_SEARCH_NAME): Use symbol_matches_search_name.
+	(symbol_matches_search_name): Declare.
+	(MSYMBOL_MATCHES_SEARCH_NAME): Delete.
+	(default_collect_symbol_completion_matches)
+	(collect_symbol_completion_matches)
+	(collect_file_symbol_completion_matches): Add name_match_type
+	parameter.
+	(iterate_over_symbols): Use lookup_name_info.
+	(completion_list_add_name): Declare.
+	* utils.c (enum class strncmp_iw_mode): Moved to utils.h.
+	(strncmp_iw_with_mode): Now extern.
+	* utils.h (enum class strncmp_iw_mode): Moved from utils.c.
+	(strncmp_iw_with_mode): Declare.
+
 2017-11-08  Keith Seitz  <keiths@redhat.com>
 	    Pedro Alves  <palves@redhat.com>
 
diff --git a/gdb/ada-lang.c b/gdb/ada-lang.c
index 7126b25..7ec19d2 100644
--- a/gdb/ada-lang.c
+++ b/gdb/ada-lang.c
@@ -104,16 +104,16 @@ static int ada_type_match (struct type *, struct type *, int);
 
 static int ada_args_match (struct symbol *, struct value **, int);
 
-static int full_match (const char *, const char *);
-
 static struct value *make_array_descriptor (struct type *, struct value *);
 
 static void ada_add_block_symbols (struct obstack *,
-                                   const struct block *, const char *,
-                                   domain_enum, struct objfile *, int);
+				   const struct block *,
+				   const lookup_name_info &lookup_name,
+				   domain_enum, struct objfile *);
 
 static void ada_add_all_symbols (struct obstack *, const struct block *,
-				 const char *, domain_enum, int, int *);
+				 const lookup_name_info &lookup_name,
+				 domain_enum, int, int *);
 
 static int is_nonfunction (struct block_symbol *, int);
 
@@ -203,7 +203,7 @@ static int is_name_suffix (const char *);
 
 static int advance_wild_match (const char **, const char *, int);
 
-static int wild_match (const char *, const char *);
+static bool wild_match (const char *name, const char *patn);
 
 static struct value *ada_coerce_ref (struct value *);
 
@@ -270,6 +270,10 @@ static void ada_forward_operator_length (struct expression *, int, int *,
 					 int *);
 
 static struct type *ada_find_any_type (const char *name);
+
+static symbol_name_matcher_ftype *ada_get_symbol_name_matcher
+  (const lookup_name_info &lookup_name);
+
 
 
 /* The result of a symbol lookup to be stored in our symbol cache.  */
@@ -976,11 +980,13 @@ const struct ada_opname_map ada_opname_table[] = {
   {NULL, NULL}
 };
 
-/* The "encoded" form of DECODED, according to GNAT conventions.
-   The result is valid until the next call to ada_encode.  */
+/* The "encoded" form of DECODED, according to GNAT conventions.  The
+   result is valid until the next call to ada_encode.  If
+   THROW_ERRORS, throw an error if invalid operator name is found.
+   Otherwise, return NULL in that case.  */
 
-char *
-ada_encode (const char *decoded)
+static char *
+ada_encode_1 (const char *decoded, bool throw_errors)
 {
   static char *encoding_buffer = NULL;
   static size_t encoding_buffer_size = 0;
@@ -1010,7 +1016,12 @@ ada_encode (const char *decoded)
                && !startswith (p, mapping->decoded); mapping += 1)
             ;
           if (mapping->encoded == NULL)
-            error (_("invalid Ada operator name: %s"), p);
+	    {
+	      if (throw_errors)
+		error (_("invalid Ada operator name: %s"), p);
+	      else
+		return NULL;
+	    }
           strcpy (encoding_buffer + k, mapping->encoded);
           k += strlen (mapping->encoded);
           break;
@@ -1026,6 +1037,15 @@ ada_encode (const char *decoded)
   return encoding_buffer;
 }
 
+/* The "encoded" form of DECODED, according to GNAT conventions.
+   The result is valid until the next call to ada_encode.  */
+
+char *
+ada_encode (const char *decoded)
+{
+  return ada_encode_1 (decoded, true);
+}
+
 /* Return NAME folded to lower case, or, if surrounded by single
    quotes, unfolded, but with the quotes stripped away.  Result good
    to next call.  */
@@ -1490,31 +1510,6 @@ ada_sniff_from_mangled_name (const char *mangled, char **out)
   return 0;
 }
 
-/* Returns non-zero iff SYM_NAME matches NAME, ignoring any trailing
-   suffixes that encode debugging information or leading _ada_ on
-   SYM_NAME (see is_name_suffix commentary for the debugging
-   information that is ignored).  If WILD, then NAME need only match a
-   suffix of SYM_NAME minus the same suffixes.  Also returns 0 if
-   either argument is NULL.  */
-
-static int
-match_name (const char *sym_name, const char *name, int wild)
-{
-  if (sym_name == NULL || name == NULL)
-    return 0;
-  else if (wild)
-    return wild_match (sym_name, name) == 0;
-  else
-    {
-      int len_name = strlen (name);
-
-      return (strncmp (sym_name, name, len_name) == 0
-              && is_name_suffix (sym_name + len_name))
-        || (startswith (sym_name, "_ada_")
-            && strncmp (sym_name + 5, name, len_name) == 0
-            && is_name_suffix (sym_name + len_name + 5));
-    }
-}
 
 
                                 /* Arrays */
@@ -3587,7 +3582,7 @@ resolve_subexp (struct expression **expp, int *pos, int deprocedure_p,
           int n_candidates;
 
           n_candidates =
-            ada_lookup_symbol_list (ada_encode (ada_decoded_op_name (op)),
+            ada_lookup_symbol_list (ada_decoded_op_name (op),
                                     (struct block *) NULL, VAR_DOMAIN,
                                     &candidates);
           i = ada_resolve_function (candidates, n_candidates, argvec, nargs,
@@ -4758,16 +4753,18 @@ cache_symbol (const char *name, domain_enum domain, struct symbol *sym,
 
                                 /* Symbol Lookup */
 
-/* Return nonzero if wild matching should be used when searching for
-   all symbols matching LOOKUP_NAME.
+/* Return the symbol name match type that should be used used when
+   searching for all symbols matching LOOKUP_NAME.
 
    LOOKUP_NAME is expected to be a symbol name after transformation
    for Ada lookups (see ada_name_for_lookup).  */
 
-static int
-should_use_wild_match (const char *lookup_name)
+static symbol_name_match_type
+name_match_type_from_name (const char *lookup_name)
 {
-  return (strstr (lookup_name, "__") == NULL);
+  return (strstr (lookup_name, "__") == NULL
+	  ? symbol_name_match_type::WILD
+	  : symbol_name_match_type::FULL);
 }
 
 /* Return the result of a standard (literal, C-like) lookup of NAME in
@@ -4937,23 +4934,18 @@ ada_lookup_simple_minsym (const char *name)
   struct bound_minimal_symbol result;
   struct objfile *objfile;
   struct minimal_symbol *msymbol;
-  const int wild_match_p = should_use_wild_match (name);
 
   memset (&result, 0, sizeof (result));
 
-  /* Special case: If the user specifies a symbol name inside package
-     Standard, do a non-wild matching of the symbol name without
-     the "standard__" prefix.  This was primarily introduced in order
-     to allow the user to specifically access the standard exceptions
-     using, for instance, Standard.Constraint_Error when Constraint_Error
-     is ambiguous (due to the user defining its own Constraint_Error
-     entity inside its program).  */
-  if (startswith (name, "standard__"))
-    name += sizeof ("standard__") - 1;
+  symbol_name_match_type match_type = name_match_type_from_name (name);
+  lookup_name_info lookup_name (name, match_type);
+
+  symbol_name_matcher_ftype *match_name
+    = ada_get_symbol_name_matcher (lookup_name);
 
   ALL_MSYMBOLS (objfile, msymbol)
   {
-    if (match_name (MSYMBOL_LINKAGE_NAME (msymbol), name, wild_match_p)
+    if (match_name (MSYMBOL_LINKAGE_NAME (msymbol), lookup_name, NULL)
         && MSYMBOL_TYPE (msymbol) != mst_solib_trampoline)
       {
 	result.minsym = msymbol;
@@ -4973,8 +4965,8 @@ ada_lookup_simple_minsym (const char *name)
 
 static void
 add_symbols_from_enclosing_procs (struct obstack *obstackp,
-                                  const char *name, domain_enum domain,
-                                  int wild_match_p)
+				  const lookup_name_info &lookup_name,
+				  domain_enum domain)
 {
 }
 
@@ -5426,17 +5418,16 @@ remove_irrelevant_renamings (struct block_symbol *syms,
    Note: This function assumes that OBSTACKP has 0 (zero) element in it.  */
 
 static void
-ada_add_local_symbols (struct obstack *obstackp, const char *name,
-                       const struct block *block, domain_enum domain,
-                       int wild_match_p)
+ada_add_local_symbols (struct obstack *obstackp,
+		       const lookup_name_info &lookup_name,
+		       const struct block *block, domain_enum domain)
 {
   int block_depth = 0;
 
   while (block != NULL)
     {
       block_depth += 1;
-      ada_add_block_symbols (obstackp, block, name, domain, NULL,
-			     wild_match_p);
+      ada_add_block_symbols (obstackp, block, lookup_name, domain, NULL);
 
       /* If we found a non-function match, assume that's the one.  */
       if (is_nonfunction (defns_collected (obstackp, 0),
@@ -5449,7 +5440,7 @@ ada_add_local_symbols (struct obstack *obstackp, const char *name,
   /* If no luck so far, try to find NAME as a local symbol in some lexically
      enclosing subprogram.  */
   if (num_defns_collected (obstackp) == 0 && block_depth > 2)
-    add_symbols_from_enclosing_procs (obstackp, name, domain, wild_match_p);
+    add_symbols_from_enclosing_procs (obstackp, lookup_name, domain);
 }
 
 /* An object of this type is used as the user_data argument when
@@ -5503,28 +5494,27 @@ aux_add_nonlocal_symbols (struct block *block, struct symbol *sym, void *data0)
   return 0;
 }
 
-/* Helper for add_nonlocal_symbols.  Find symbols in DOMAIN which are targetted
-   by renamings matching NAME in BLOCK.  Add these symbols to OBSTACKP.  If
-   WILD_MATCH_P is nonzero, perform the naming matching in "wild" mode (see
-   function "wild_match" for more information).  Return whether we found such
-   symbols.  */
+/* Helper for add_nonlocal_symbols.  Find symbols in DOMAIN which are
+   targeted by renamings matching LOOKUP_NAME in BLOCK.  Add these
+   symbols to OBSTACKP.  Return whether we found such symbols.  */
 
 static int
 ada_add_block_renamings (struct obstack *obstackp,
 			 const struct block *block,
-			 const char *name,
-			 domain_enum domain,
-			 int wild_match_p)
+			 const lookup_name_info &lookup_name,
+			 domain_enum domain)
 {
   struct using_direct *renaming;
   int defns_mark = num_defns_collected (obstackp);
 
+  symbol_name_matcher_ftype *name_match
+    = ada_get_symbol_name_matcher (lookup_name);
+
   for (renaming = block_using (block);
        renaming != NULL;
        renaming = renaming->next)
     {
       const char *r_name;
-      int name_match;
 
       /* Avoid infinite recursions: skip this renaming if we are actually
 	 already traversing it.
@@ -5549,11 +5539,13 @@ ada_add_block_renamings (struct obstack *obstackp,
       r_name = (renaming->alias != NULL
 		? renaming->alias
 		: renaming->declaration);
-      name_match
-	= wild_match_p ? wild_match (r_name, name) : strcmp (r_name, name);
-      if (name_match == 0)
-	ada_add_all_symbols (obstackp, block, renaming->declaration, domain,
-			     1, NULL);
+      if (name_match (r_name, lookup_name, NULL))
+	{
+	  lookup_name_info decl_lookup_name (renaming->declaration,
+					     lookup_name.match_type ());
+	  ada_add_all_symbols (obstackp, block, decl_lookup_name, domain,
+			       1, NULL);
+	}
       renaming->searched = 0;
     }
   return num_defns_collected (obstackp) != defns_mark;
@@ -5644,14 +5636,24 @@ compare_names (const char *string1, const char *string2)
   return result;
 }
 
+/* Convenience function to get at the Ada encoded lookup name for
+   LOOKUP_NAME, as a C string.  */
+
+static const char *
+ada_lookup_name (const lookup_name_info &lookup_name)
+{
+  return lookup_name.ada ().lookup_name ().c_str ();
+}
+
 /* Add to OBSTACKP all non-local symbols whose name and domain match
-   NAME and DOMAIN respectively.  The search is performed on GLOBAL_BLOCK
-   symbols if GLOBAL is non-zero, or on STATIC_BLOCK symbols otherwise.  */
+   LOOKUP_NAME and DOMAIN respectively.  The search is performed on
+   GLOBAL_BLOCK symbols if GLOBAL is non-zero, or on STATIC_BLOCK
+   symbols otherwise.  */
 
 static void
-add_nonlocal_symbols (struct obstack *obstackp, const char *name,
-		      domain_enum domain, int global,
-		      int is_wild_match)
+add_nonlocal_symbols (struct obstack *obstackp,
+		      const lookup_name_info &lookup_name,
+		      domain_enum domain, int global)
 {
   struct objfile *objfile;
   struct compunit_symtab *cu;
@@ -5660,50 +5662,57 @@ add_nonlocal_symbols (struct obstack *obstackp, const char *name,
   memset (&data, 0, sizeof data);
   data.obstackp = obstackp;
 
+  bool is_wild_match = lookup_name.ada ().wild_match_p ();
+
   ALL_OBJFILES (objfile)
     {
       data.objfile = objfile;
 
       if (is_wild_match)
-	objfile->sf->qf->map_matching_symbols (objfile, name, domain, global,
+	objfile->sf->qf->map_matching_symbols (objfile, lookup_name.name ().c_str (),
+					       domain, global,
 					       aux_add_nonlocal_symbols, &data,
-					       wild_match, NULL);
+					       symbol_name_match_type::WILD,
+					       NULL);
       else
-	objfile->sf->qf->map_matching_symbols (objfile, name, domain, global,
+	objfile->sf->qf->map_matching_symbols (objfile, lookup_name.name ().c_str (),
+					       domain, global,
 					       aux_add_nonlocal_symbols, &data,
-					       full_match, compare_names);
+					       symbol_name_match_type::FULL,
+					       compare_names);
 
       ALL_OBJFILE_COMPUNITS (objfile, cu)
 	{
 	  const struct block *global_block
 	    = BLOCKVECTOR_BLOCK (COMPUNIT_BLOCKVECTOR (cu), GLOBAL_BLOCK);
 
-	  if (ada_add_block_renamings (obstackp, global_block , name, domain,
-				       is_wild_match))
+	  if (ada_add_block_renamings (obstackp, global_block, lookup_name,
+				       domain))
 	    data.found_sym = 1;
 	}
     }
 
   if (num_defns_collected (obstackp) == 0 && global && !is_wild_match)
     {
+      const char *name = ada_lookup_name (lookup_name);
+      std::string name1 = std::string ("<_ada_") + name + '>';
+
       ALL_OBJFILES (objfile)
         {
-	  char *name1 = (char *) alloca (strlen (name) + sizeof ("_ada_"));
-	  strcpy (name1, "_ada_");
-	  strcpy (name1 + sizeof ("_ada_") - 1, name);
 	  data.objfile = objfile;
-	  objfile->sf->qf->map_matching_symbols (objfile, name1, domain,
-						 global,
+	  objfile->sf->qf->map_matching_symbols (objfile, name1.c_str (),
+						 domain, global,
 						 aux_add_nonlocal_symbols,
 						 &data,
-						 full_match, compare_names);
+						 symbol_name_match_type::FULL,
+						 compare_names);
 	}
     }      	
 }
 
-/* Find symbols in DOMAIN matching NAME, in BLOCK and, if FULL_SEARCH is
-   non-zero, enclosing scope and in global scopes, returning the number of
-   matches.  Add these to OBSTACKP.
+/* Find symbols in DOMAIN matching LOOKUP_NAME, in BLOCK and, if
+   FULL_SEARCH is non-zero, enclosing scope and in global scopes,
+   returning the number of matches.  Add these to OBSTACKP.
 
    When FULL_SEARCH is non-zero, any non-function/non-enumeral
    symbol match within the nest of blocks whose innermost member is BLOCK,
@@ -5711,8 +5720,9 @@ add_nonlocal_symbols (struct obstack *obstackp, const char *name,
    enclosing blocks is returned).  If there are any matches in or
    surrounding BLOCK, then these alone are returned.
 
-   Names prefixed with "standard__" are handled specially: "standard__"
-   is first stripped off, and only static and global symbols are searched.
+   Names prefixed with "standard__" are handled specially:
+   "standard__" is first stripped off (by the lookup_name
+   constructor), and only static and global symbols are searched.
 
    If MADE_GLOBAL_LOOKUP_P is non-null, set it before return to whether we had
    to lookup global symbols.  */
@@ -5720,13 +5730,12 @@ add_nonlocal_symbols (struct obstack *obstackp, const char *name,
 static void
 ada_add_all_symbols (struct obstack *obstackp,
 		     const struct block *block,
-		     const char *name,
+		     const lookup_name_info &lookup_name,
 		     domain_enum domain,
 		     int full_search,
 		     int *made_global_lookup_p)
 {
   struct symbol *sym;
-  const int wild_match_p = should_use_wild_match (name);
 
   if (made_global_lookup_p)
     *made_global_lookup_p = 0;
@@ -5738,25 +5747,21 @@ ada_add_all_symbols (struct obstack *obstackp,
      using, for instance, Standard.Constraint_Error when Constraint_Error
      is ambiguous (due to the user defining its own Constraint_Error
      entity inside its program).  */
-  if (startswith (name, "standard__"))
-    {
-      block = NULL;
-      name = name + sizeof ("standard__") - 1;
-    }
+  if (lookup_name.ada ().standard_p ())
+    block = NULL;
 
   /* Check the non-global symbols.  If we have ANY match, then we're done.  */
 
   if (block != NULL)
     {
       if (full_search)
-	ada_add_local_symbols (obstackp, name, block, domain, wild_match_p);
+	ada_add_local_symbols (obstackp, lookup_name, block, domain);
       else
 	{
 	  /* In the !full_search case we're are being called by
 	     ada_iterate_over_symbols, and we don't want to search
 	     superblocks.  */
-	  ada_add_block_symbols (obstackp, block, name, domain, NULL,
-				 wild_match_p);
+	  ada_add_block_symbols (obstackp, block, lookup_name, domain, NULL);
 	}
       if (num_defns_collected (obstackp) > 0 || !full_search)
 	return;
@@ -5766,10 +5771,11 @@ ada_add_all_symbols (struct obstack *obstackp,
      already performed this search before.  If we have, then return
      the same result.  */
 
-  if (lookup_cached_symbol (name, domain, &sym, &block))
+  if (lookup_cached_symbol (ada_lookup_name (lookup_name),
+			    domain, &sym, &block))
     {
       if (sym != NULL)
-        add_defn_to_vec (obstackp, sym, block);
+	add_defn_to_vec (obstackp, sym, block);
       return;
     }
 
@@ -5778,17 +5784,17 @@ ada_add_all_symbols (struct obstack *obstackp,
 
   /* Search symbols from all global blocks.  */
  
-  add_nonlocal_symbols (obstackp, name, domain, 1, wild_match_p);
+  add_nonlocal_symbols (obstackp, lookup_name, domain, 1);
 
   /* Now add symbols from all per-file blocks if we've gotten no hits
      (not strictly correct, but perhaps better than an error).  */
 
   if (num_defns_collected (obstackp) == 0)
-    add_nonlocal_symbols (obstackp, name, domain, 0, wild_match_p);
+    add_nonlocal_symbols (obstackp, lookup_name, domain, 0);
 }
 
-/* Find symbols in DOMAIN matching NAME, in BLOCK and, if full_search is
-   non-zero, enclosing scope and in global scopes, returning the number of
+/* Find symbols in DOMAIN matching LOOKUP_NAME, in BLOCK and, if FULL_SEARCH
+   is non-zero, enclosing scope and in global scopes, returning the number of
    matches.
    Sets *RESULTS to point to a vector of (SYM,BLOCK) tuples,
    indicating the symbols found and the blocks and symbol tables (if
@@ -5805,19 +5811,19 @@ ada_add_all_symbols (struct obstack *obstackp,
    is first stripped off, and only static and global symbols are searched.  */
 
 static int
-ada_lookup_symbol_list_worker (const char *name, const struct block *block,
+ada_lookup_symbol_list_worker (const lookup_name_info &lookup_name,
+			       const struct block *block,
 			       domain_enum domain,
 			       struct block_symbol **results,
 			       int full_search)
 {
-  const int wild_match_p = should_use_wild_match (name);
   int syms_from_global_search;
   int ndefns;
 
   obstack_free (&symbol_list_obstack, NULL);
   obstack_init (&symbol_list_obstack);
-  ada_add_all_symbols (&symbol_list_obstack, block, name, domain,
-		       full_search, &syms_from_global_search);
+  ada_add_all_symbols (&symbol_list_obstack, block, lookup_name,
+		       domain, full_search, &syms_from_global_search);
 
   ndefns = num_defns_collected (&symbol_list_obstack);
   *results = defns_collected (&symbol_list_obstack, 1);
@@ -5825,32 +5831,37 @@ ada_lookup_symbol_list_worker (const char *name, const struct block *block,
   ndefns = remove_extra_symbols (*results, ndefns);
 
   if (ndefns == 0 && full_search && syms_from_global_search)
-    cache_symbol (name, domain, NULL, NULL);
+    cache_symbol (ada_lookup_name (lookup_name), domain, NULL, NULL);
 
   if (ndefns == 1 && full_search && syms_from_global_search)
-    cache_symbol (name, domain, (*results)[0].symbol, (*results)[0].block);
+    cache_symbol (ada_lookup_name (lookup_name), domain,
+		  (*results)[0].symbol, (*results)[0].block);
 
   ndefns = remove_irrelevant_renamings (*results, ndefns, block);
   return ndefns;
 }
 
-/* Find symbols in DOMAIN matching NAME0, in BLOCK0 and enclosing scope and
+/* Find symbols in DOMAIN matching NAME, in BLOCK and enclosing scope and
    in global scopes, returning the number of matches, and setting *RESULTS
    to a vector of (SYM,BLOCK) tuples.
    See ada_lookup_symbol_list_worker for further details.  */
 
 int
-ada_lookup_symbol_list (const char *name0, const struct block *block0,
+ada_lookup_symbol_list (const char *name, const struct block *block,
 			domain_enum domain, struct block_symbol **results)
 {
-  return ada_lookup_symbol_list_worker (name0, block0, domain, results, 1);
+  symbol_name_match_type name_match_type = name_match_type_from_name (name);
+  lookup_name_info lookup_name (name, name_match_type);
+
+  return ada_lookup_symbol_list_worker (lookup_name, block, domain, results, 1);
 }
 
 /* Implementation of the la_iterate_over_symbols method.  */
 
 static void
 ada_iterate_over_symbols
-  (const struct block *block, const char *name, domain_enum domain,
+  (const struct block *block, const lookup_name_info &name,
+   domain_enum domain,
    gdb::function_view<symbol_found_callback_ftype> callback)
 {
   int ndefs, i;
@@ -5864,24 +5875,6 @@ ada_iterate_over_symbols
     }
 }
 
-/* If NAME is the name of an entity, return a string that should
-   be used to look that entity up in Ada units.
-
-   NAME can have any form that the "break" or "print" commands might
-   recognize.  In other words, it does not have to be the "natural"
-   name, or the "encoded" name.  */
-
-std::string
-ada_name_for_lookup (const char *name)
-{
-  int nlen = strlen (name);
-
-  if (name[0] == '<' && name[nlen - 1] == '>')
-    return std::string (name + 1, nlen - 2);
-  else
-    return ada_encode (ada_fold_name (name));
-}
-
 /* The result is as for ada_lookup_symbol_list with FULL_SEARCH set
    to 1, but choosing the first symbol found if there are multiple
    choices.
@@ -5897,10 +5890,19 @@ ada_lookup_encoded_symbol (const char *name, const struct block *block,
   struct block_symbol *candidates;
   int n_candidates;
 
+  /* Since we already have an encoded name, wrap it in '<>' to force a
+     verbatim match.  Otherwise, if the name happens to not look like
+     an encoded name (because it doesn't include a "__"),
+     ada_lookup_name_info would re-encode/fold it again, and that
+     would e.g., incorrectly lowercase object renaming names like
+     "R28b" -> "r28b".  */
+  std::string verbatim = std::string ("<") + name + '>';
+
   gdb_assert (info != NULL);
   memset (info, 0, sizeof (struct block_symbol));
 
-  n_candidates = ada_lookup_symbol_list (name, block, domain, &candidates);
+  n_candidates = ada_lookup_symbol_list (verbatim.c_str (), block,
+					 domain, &candidates);
   if (n_candidates == 0)
     return;
 
@@ -6181,11 +6183,12 @@ advance_wild_match (const char **namep, const char *name0, int target0)
   return 1;
 }
 
-/* Return 0 iff NAME encodes a name of the form prefix.PATN.  Ignores any
-   informational suffixes of NAME (i.e., for which is_name_suffix is
-   true).  Assumes that PATN is a lower-cased Ada simple name.  */
+/* Return true iff NAME encodes a name of the form prefix.PATN.
+   Ignores any informational suffixes of NAME (i.e., for which
+   is_name_suffix is true).  Assumes that PATN is a lower-cased Ada
+   simple name.  */
 
-static int
+static bool
 wild_match (const char *name, const char *patn)
 {
   const char *p;
@@ -6201,39 +6204,49 @@ wild_match (const char *name, const char *patn)
 	    if (*p != *name)
 	      break;
 	  if (*p == '\0' && is_name_suffix (name))
-	    return match != name0 && !is_valid_name_for_wild_match (name0);
+	    return match == name0 || is_valid_name_for_wild_match (name0);
 
 	  if (name[-1] == '_')
 	    name -= 1;
 	}
       if (!advance_wild_match (&name, name0, *patn))
-	return 1;
+	return false;
     }
 }
 
-/* Returns 0 iff symbol name SYM_NAME matches SEARCH_NAME, apart from
-   informational suffix.  */
+/* Returns true iff symbol name SYM_NAME matches SEARCH_NAME, ignoring
+   any trailing suffixes that encode debugging information or leading
+   _ada_ on SYM_NAME (see is_name_suffix commentary for the debugging
+   information that is ignored).  */
 
-static int
+static bool
 full_match (const char *sym_name, const char *search_name)
 {
-  return !match_name (sym_name, search_name, 0);
-}
+  size_t search_name_len = strlen (search_name);
+
+  if (strncmp (sym_name, search_name, search_name_len) == 0
+      && is_name_suffix (sym_name + search_name_len))
+    return true;
+
+  if (startswith (sym_name, "_ada_")
+      && strncmp (sym_name + 5, search_name, search_name_len) == 0
+      && is_name_suffix (sym_name + search_name_len + 5))
+    return true;
 
+  return false;
+}
 
-/* Add symbols from BLOCK matching identifier NAME in DOMAIN to
-   vector *defn_symbols, updating the list of symbols in OBSTACKP 
-   (if necessary).  If WILD, treat as NAME with a wildcard prefix.
-   OBJFILE is the section containing BLOCK.  */
+/* Add symbols from BLOCK matching LOOKUP_NAME in DOMAIN to vector
+   *defn_symbols, updating the list of symbols in OBSTACKP (if
+   necessary).  OBJFILE is the section containing BLOCK.  */
 
 static void
 ada_add_block_symbols (struct obstack *obstackp,
-                       const struct block *block, const char *name,
-                       domain_enum domain, struct objfile *objfile,
-                       int wild)
+		       const struct block *block,
+		       const lookup_name_info &lookup_name,
+		       domain_enum domain, struct objfile *objfile)
 {
   struct block_iterator iter;
-  int name_len = strlen (name);
   /* A matching argument symbol, if any.  */
   struct symbol *arg_sym;
   /* Set true when we find a matching non-argument symbol.  */
@@ -6242,56 +6255,31 @@ ada_add_block_symbols (struct obstack *obstackp,
 
   arg_sym = NULL;
   found_sym = 0;
-  if (wild)
+  for (sym = block_iter_match_first (block, lookup_name, &iter);
+       sym != NULL;
+       sym = block_iter_match_next (lookup_name, &iter))
     {
-      for (sym = block_iter_match_first (block, name, wild_match, &iter);
-	   sym != NULL; sym = block_iter_match_next (name, wild_match, &iter))
-      {
-        if (symbol_matches_domain (SYMBOL_LANGUAGE (sym),
-                                   SYMBOL_DOMAIN (sym), domain)
-            && wild_match (SYMBOL_LINKAGE_NAME (sym), name) == 0)
-          {
-	    if (SYMBOL_CLASS (sym) == LOC_UNRESOLVED)
-	      continue;
-	    else if (SYMBOL_IS_ARGUMENT (sym))
-	      arg_sym = sym;
-	    else
-	      {
-                found_sym = 1;
-                add_defn_to_vec (obstackp,
-                                 fixup_symbol_section (sym, objfile),
-                                 block);
-              }
-          }
-      }
-    }
-  else
-    {
-     for (sym = block_iter_match_first (block, name, full_match, &iter);
-	  sym != NULL; sym = block_iter_match_next (name, full_match, &iter))
-      {
-        if (symbol_matches_domain (SYMBOL_LANGUAGE (sym),
-                                   SYMBOL_DOMAIN (sym), domain))
-          {
-	    if (SYMBOL_CLASS (sym) != LOC_UNRESOLVED)
-	      {
-		if (SYMBOL_IS_ARGUMENT (sym))
-		  arg_sym = sym;
-		else
-		  {
-		    found_sym = 1;
-		    add_defn_to_vec (obstackp,
-				     fixup_symbol_section (sym, objfile),
-				     block);
-		  }
-	      }
-          }
-      }
+      if (symbol_matches_domain (SYMBOL_LANGUAGE (sym),
+				 SYMBOL_DOMAIN (sym), domain))
+	{
+	  if (SYMBOL_CLASS (sym) != LOC_UNRESOLVED)
+	    {
+	      if (SYMBOL_IS_ARGUMENT (sym))
+		arg_sym = sym;
+	      else
+		{
+		  found_sym = 1;
+		  add_defn_to_vec (obstackp,
+				   fixup_symbol_section (sym, objfile),
+				   block);
+		}
+	    }
+	}
     }
 
   /* Handle renamings.  */
 
-  if (ada_add_block_renamings (obstackp, block, name, domain, wild))
+  if (ada_add_block_renamings (obstackp, block, lookup_name, domain))
     found_sym = 1;
 
   if (!found_sym && arg_sym != NULL)
@@ -6301,10 +6289,13 @@ ada_add_block_symbols (struct obstack *obstackp,
                        block);
     }
 
-  if (!wild)
+  if (!lookup_name.ada ().wild_match_p ())
     {
       arg_sym = NULL;
       found_sym = 0;
+      const std::string &ada_lookup_name = lookup_name.ada ().lookup_name ();
+      const char *name = ada_lookup_name.c_str ();
+      size_t name_len = ada_lookup_name.size ();
 
       ALL_BLOCK_SYMBOLS (block, iter, sym)
       {
@@ -6355,51 +6346,39 @@ ada_add_block_symbols (struct obstack *obstackp,
 
                                 /* Symbol Completion */
 
-/* If SYM_NAME is a completion candidate for TEXT, return this symbol
-   name in a form that's appropriate for the completion.  The result
-   does not need to be deallocated, but is only good until the next call.
-
-   TEXT_LEN is equal to the length of TEXT.
-   Perform a wild match if WILD_MATCH_P is set.
-   ENCODED_P should be set if TEXT represents the start of a symbol name
-   in its encoded form.  */
+/* See symtab.h.  */
 
-static const char *
-symbol_completion_match (const char *sym_name,
-                         const char *text, int text_len,
-                         int wild_match_p, int encoded_p)
+bool
+ada_lookup_name_info::matches
+  (const char *sym_name,
+   symbol_name_match_type match_type,
+   completion_match *comp_match) const
 {
-  const int verbatim_match = (text[0] == '<');
-  int match = 0;
-
-  if (verbatim_match)
-    {
-      /* Strip the leading angle bracket.  */
-      text = text + 1;
-      text_len--;
-    }
+  bool match = false;
+  const char *text = m_encoded_name.c_str ();
+  size_t text_len = m_encoded_name.size ();
 
   /* First, test against the fully qualified name of the symbol.  */
 
   if (strncmp (sym_name, text, text_len) == 0)
-    match = 1;
+    match = true;
 
-  if (match && !encoded_p)
+  if (match && !m_encoded_p)
     {
       /* One needed check before declaring a positive match is to verify
          that iff we are doing a verbatim match, the decoded version
          of the symbol name starts with '<'.  Otherwise, this symbol name
          is not a suitable completion.  */
       const char *sym_name_copy = sym_name;
-      int has_angle_bracket;
+      bool has_angle_bracket;
 
       sym_name = ada_decode (sym_name);
       has_angle_bracket = (sym_name[0] == '<');
-      match = (has_angle_bracket == verbatim_match);
+      match = (has_angle_bracket == m_verbatim_p);
       sym_name = sym_name_copy;
     }
 
-  if (match && !verbatim_match)
+  if (match && !m_verbatim_p)
     {
       /* When doing non-verbatim match, another check that needs to
          be done is to verify that the potentially matching symbol name
@@ -6410,12 +6389,12 @@ symbol_completion_match (const char *sym_name,
 
       for (tmp = sym_name; *tmp != '\0' && !isupper (*tmp); tmp++);
       if (*tmp != '\0')
-        match = 0;
+	match = false;
     }
 
   /* Second: Try wild matching...  */
 
-  if (!match && wild_match_p)
+  if (!match && m_wild_match_p)
     {
       /* Since we are doing wild matching, this means that TEXT
          may represent an unqualified symbol name.  We therefore must
@@ -6423,91 +6402,48 @@ symbol_completion_match (const char *sym_name,
       sym_name = ada_unqualified_name (ada_decode (sym_name));
 
       if (strncmp (sym_name, text, text_len) == 0)
-        match = 1;
+	match = true;
     }
 
-  /* Finally: If we found a mach, prepare the result to return.  */
+  /* Finally: If we found a match, prepare the result to return.  */
 
   if (!match)
-    return NULL;
-
-  if (verbatim_match)
-    sym_name = add_angle_brackets (sym_name);
-
-  if (!encoded_p)
-    sym_name = ada_decode (sym_name);
-
-  return sym_name;
-}
-
-/* A companion function to ada_collect_symbol_completion_matches().
-   Check if SYM_NAME represents a symbol which name would be suitable
-   to complete TEXT (TEXT_LEN is the length of TEXT), in which case it
-   is added as a completion match to TRACKER.
-
-   ORIG_TEXT is the string original string from the user command
-   that needs to be completed.  WORD is the entire command on which
-   completion should be performed.  These two parameters are used to
-   determine which part of the symbol name should be added to the
-   completion vector.
-   if WILD_MATCH_P is set, then wild matching is performed.
-   ENCODED_P should be set if TEXT represents a symbol name in its
-   encoded formed (in which case the completion should also be
-   encoded).  */
-
-static void
-symbol_completion_add (completion_tracker &tracker,
-		       const char *sym_name,
-                       const char *text, int text_len,
-                       const char *orig_text, const char *word,
-                       int wild_match_p, int encoded_p)
-{
-  const char *match = symbol_completion_match (sym_name, text, text_len,
-                                               wild_match_p, encoded_p);
-  char *completion;
+    return false;
 
-  if (match == NULL)
-    return;
+  if (comp_match != NULL)
+    {
+      std::string &match_str = comp_match->storage ();
 
-  /* We found a match, so add the appropriate completion to the given
-     string vector.  */
+      if (!m_encoded_p)
+	{
+	  match_str = ada_decode (sym_name);
+	  comp_match->set_match (match_str.c_str ());
+	}
+      else
+	{
+	  if (m_verbatim_p)
+	    match_str = add_angle_brackets (sym_name);
+	  else
+	    match_str = sym_name;
 
-  if (word == orig_text)
-    {
-      completion = (char *) xmalloc (strlen (match) + 5);
-      strcpy (completion, match);
-    }
-  else if (word > orig_text)
-    {
-      /* Return some portion of sym_name.  */
-      completion = (char *) xmalloc (strlen (match) + 5);
-      strcpy (completion, match + (word - orig_text));
-    }
-  else
-    {
-      /* Return some of ORIG_TEXT plus sym_name.  */
-      completion = (char *) xmalloc (strlen (match) + (orig_text - word) + 5);
-      strncpy (completion, word, orig_text - word);
-      completion[orig_text - word] = '\0';
-      strcat (completion, match);
+	  comp_match->set_match (match_str.c_str ());
+	}
     }
 
-  tracker.add_completion (gdb::unique_xmalloc_ptr<char> (completion));
+  return true;
 }
 
-/* Add the list of possible symbol names completing TEXT0 to TRACKER.
+/* Add the list of possible symbol names completing TEXT to TRACKER.
    WORD is the entire command on which completion is made.  */
 
 static void
 ada_collect_symbol_completion_matches (completion_tracker &tracker,
 				       complete_symbol_mode mode,
-				       const char *text0, const char *word,
+				       symbol_name_match_type name_match_type,
+				       const char *text, const char *word,
 				       enum type_code code)
 {
-  char *text;
   int text_len;
-  int wild_match_p;
-  int encoded_p;
   struct symbol *sym;
   struct compunit_symtab *s;
   struct minimal_symbol *msymbol;
@@ -6519,39 +6455,15 @@ ada_collect_symbol_completion_matches (completion_tracker &tracker,
 
   gdb_assert (code == TYPE_CODE_UNDEF);
 
-  if (text0[0] == '<')
-    {
-      text = xstrdup (text0);
-      make_cleanup (xfree, text);
-      text_len = strlen (text);
-      wild_match_p = 0;
-      encoded_p = 1;
-    }
-  else
-    {
-      text = xstrdup (ada_encode (text0));
-      make_cleanup (xfree, text);
-      text_len = strlen (text);
-      for (i = 0; i < text_len; i++)
-        text[i] = tolower (text[i]);
+  text_len = strlen (text);
 
-      encoded_p = (strstr (text0, "__") != NULL);
-      /* If the name contains a ".", then the user is entering a fully
-         qualified entity name, and the match must not be done in wild
-         mode.  Similarly, if the user wants to complete what looks like
-         an encoded name, the match must not be done in wild mode.  */
-      wild_match_p = (strchr (text0, '.') == NULL && !encoded_p);
-    }
+  lookup_name_info lookup_name (std::string (text, text_len),
+				name_match_type, true);
 
   /* First, look at the partial symtab symbols.  */
   expand_symtabs_matching (NULL,
-			   [&] (const char *symname)
-			   {
-			     return symbol_completion_match (symname,
-							     text, text_len,
-							     wild_match_p,
-							     encoded_p);
-			   },
+			   lookup_name,
+			   NULL,
 			   NULL,
 			   ALL_DOMAIN);
 
@@ -6563,9 +6475,12 @@ ada_collect_symbol_completion_matches (completion_tracker &tracker,
   ALL_MSYMBOLS (objfile, msymbol)
   {
     QUIT;
-    symbol_completion_add (tracker, MSYMBOL_LINKAGE_NAME (msymbol),
-			   text, text_len, text0, word, wild_match_p,
-			   encoded_p);
+
+    completion_list_add_name (tracker,
+			      MSYMBOL_LANGUAGE (msymbol),
+			      MSYMBOL_LINKAGE_NAME (msymbol),
+			      lookup_name,
+			      text, text_len, text, word);
   }
 
   /* Search upwards from currently selected frame (so that we can
@@ -6578,9 +6493,11 @@ ada_collect_symbol_completion_matches (completion_tracker &tracker,
 
       ALL_BLOCK_SYMBOLS (b, iter, sym)
       {
-	symbol_completion_add (tracker, SYMBOL_LINKAGE_NAME (sym),
-                               text, text_len, text0, word,
-                               wild_match_p, encoded_p);
+	completion_list_add_name (tracker,
+				  SYMBOL_LANGUAGE (sym),
+				  SYMBOL_LINKAGE_NAME (sym),
+				  lookup_name,
+				  text, text_len, text, word);
       }
     }
 
@@ -6593,9 +6510,11 @@ ada_collect_symbol_completion_matches (completion_tracker &tracker,
     b = BLOCKVECTOR_BLOCK (COMPUNIT_BLOCKVECTOR (s), GLOBAL_BLOCK);
     ALL_BLOCK_SYMBOLS (b, iter, sym)
     {
-      symbol_completion_add (tracker, SYMBOL_LINKAGE_NAME (sym),
-                             text, text_len, text0, word,
-                             wild_match_p, encoded_p);
+      completion_list_add_name (tracker,
+				SYMBOL_LANGUAGE (sym),
+				SYMBOL_LINKAGE_NAME (sym),
+				lookup_name,
+				text, text_len, text, word);
     }
   }
 
@@ -6608,9 +6527,11 @@ ada_collect_symbol_completion_matches (completion_tracker &tracker,
       continue;
     ALL_BLOCK_SYMBOLS (b, iter, sym)
     {
-      symbol_completion_add (tracker, SYMBOL_LINKAGE_NAME (sym),
-                             text, text_len, text0, word,
-                             wild_match_p, encoded_p);
+      completion_list_add_name (tracker,
+				SYMBOL_LANGUAGE (sym),
+				SYMBOL_LINKAGE_NAME (sym),
+				lookup_name,
+				text, text_len, text, word);
     }
   }
 
@@ -11583,11 +11504,12 @@ scan_discrim_bound (const char *str, int k, struct value *dval, LONGEST * px,
 static struct value *
 get_var_value (const char *name, const char *err_msg)
 {
-  struct block_symbol *syms;
-  int nsyms;
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
 
-  nsyms = ada_lookup_symbol_list (name, get_selected_block (0), VAR_DOMAIN,
-                                  &syms);
+  struct block_symbol *syms;
+  int nsyms = ada_lookup_symbol_list_worker (lookup_name,
+					     get_selected_block (0),
+					     VAR_DOMAIN, &syms, 1);
 
   if (nsyms != 1)
     {
@@ -13260,6 +13182,7 @@ ada_add_global_exceptions (compiled_regex *preg,
      regular expression used to do the matching refers to the natural
      name.  So match against the decoded name.  */
   expand_symtabs_matching (NULL,
+			   lookup_name_info::match_any (),
 			   [&] (const char *search_name)
 			   {
 			     const char *decoded = ada_decode (search_name);
@@ -13859,16 +13782,113 @@ static const struct exp_descriptor ada_exp_descriptor = {
   ada_evaluate_subexp
 };
 
-/* Implement the "la_get_symbol_name_cmp" language_defn method
-   for Ada.  */
+/* symbol_name_matcher_ftype adapter for wild_match.  */
+
+static bool
+do_wild_match (const char *symbol_search_name,
+	       const lookup_name_info &lookup_name,
+	       completion_match *match)
+{
+  return wild_match (symbol_search_name, ada_lookup_name (lookup_name));
+}
+
+/* symbol_name_matcher_ftype adapter for full_match.  */
+
+static bool
+do_full_match (const char *symbol_search_name,
+	       const lookup_name_info &lookup_name,
+	       completion_match *match)
+{
+  return full_match (symbol_search_name, ada_lookup_name (lookup_name));
+}
+
+/* Build the Ada lookup name for LOOKUP_NAME.  */
+
+ada_lookup_name_info::ada_lookup_name_info (const lookup_name_info &lookup_name)
+{
+  const std::string &user_name = lookup_name.name ();
+
+  if (user_name[0] == '<')
+    {
+      if (user_name.back () == '>')
+	m_encoded_name = user_name.substr (1, user_name.size () - 2);
+      else
+	m_encoded_name = user_name.substr (1, user_name.size () - 1);
+      m_encoded_p = true;
+      m_verbatim_p = true;
+      m_wild_match_p = false;
+      m_standard_p = false;
+    }
+  else
+    {
+      m_verbatim_p = false;
+
+      m_encoded_p = user_name.find ("__") != std::string::npos;
+
+      if (!m_encoded_p)
+	{
+	  const char *folded = ada_fold_name (user_name.c_str ());
+	  const char *encoded = ada_encode_1 (folded, false);
+	  if (encoded != NULL)
+	    m_encoded_name = encoded;
+	  else
+	    m_encoded_name = user_name;
+	}
+      else
+	m_encoded_name = user_name;
+
+      /* Handle the 'package Standard' special case.  See description
+	 of m_standard_p.  */
+      if (startswith (m_encoded_name.c_str (), "standard__"))
+	{
+	  m_encoded_name = m_encoded_name.substr (sizeof ("standard__") - 1);
+	  m_standard_p = true;
+	}
+      else
+	m_standard_p = false;
 
-static symbol_name_cmp_ftype
-ada_get_symbol_name_cmp (const char *lookup_name)
+      /* If the name contains a ".", then the user is entering a fully
+	 qualified entity name, and the match must not be done in wild
+	 mode.  Similarly, if the user wants to complete what looks
+	 like an encoded name, the match must not be done in wild
+	 mode.  Also, in the standard__ special case always do
+	 non-wild matching.  */
+      m_wild_match_p
+	= (lookup_name.match_type () != symbol_name_match_type::FULL
+	   && !m_encoded_p
+	   && !m_standard_p
+	   && user_name.find ('.') == std::string::npos);
+    }
+}
+
+/* symbol_name_matcher_ftype method for Ada.  This only handles
+   completion mode.  */
+
+static bool
+ada_symbol_name_matches (const char *symbol_search_name,
+			 const lookup_name_info &lookup_name,
+			 completion_match *match)
 {
-  if (should_use_wild_match (lookup_name))
-    return wild_match;
+  return lookup_name.ada ().matches (symbol_search_name,
+				     lookup_name.match_type (),
+				     match);
+}
+
+/* Implement the "la_get_symbol_name_matcher" language_defn method for
+   Ada.  */
+
+static symbol_name_matcher_ftype *
+ada_get_symbol_name_matcher (const lookup_name_info &lookup_name)
+{
+  if (lookup_name.completion_mode ())
+    return ada_symbol_name_matches;
   else
-    return compare_names;
+    {
+      if (lookup_name.ada ().wild_match_p ())
+	return do_wild_match;
+      else
+	return do_full_match;
+    }
 }
 
 /* Implement the "la_read_var_value" language_defn method for Ada.  */
@@ -13939,7 +13959,7 @@ extern const struct language_defn ada_language_defn = {
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  ada_get_symbol_name_cmp,	/* la_get_symbol_name_cmp */
+  ada_get_symbol_name_matcher,	/* la_get_symbol_name_matcher */
   ada_iterate_over_symbols,
   default_search_name_hash,
   &ada_varobj_ops,
diff --git a/gdb/ada-lex.l b/gdb/ada-lex.l
index ce8de69..63137bd 100644
--- a/gdb/ada-lex.l
+++ b/gdb/ada-lex.l
@@ -416,13 +416,12 @@ processReal (struct parser_state *par_state, const char *num0)
 /* Store a canonicalized version of NAME0[0..LEN-1] in yylval.ssym.  The
    resulting string is valid until the next call to ada_parse.  If
    NAME0 contains the substring "___", it is assumed to be already
-   encoded and the resulting name is equal to it.  Otherwise, it differs
+   encoded and the resulting name is equal to it.  Similarly, if the name
+   starts with '<', it is copied verbatim.  Otherwise, it differs
    from NAME0 in that:
-    + Characters between '...' or <...> are transfered verbatim to 
-      yylval.ssym.
-    + <, >, and trailing "'" characters in quoted sequences are removed
-      (a leading quote is preserved to indicate that the name is not to be
-      GNAT-encoded).
+    + Characters between '...' are transfered verbatim to yylval.ssym.
+    + Trailing "'" characters in quoted sequences are removed (a leading quote is
+      preserved to indicate that the name is not to be GNAT-encoded).
     + Unquoted whitespace is removed.
     + Unquoted alphabetic characters are mapped to lower case.
    Result is returned as a struct stoken, but for convenience, the string
@@ -440,7 +439,7 @@ processId (const char *name0, int len)
   while (len > 0 && isspace (name0[len-1]))
     len -= 1;
 
-  if (strstr (name0, "___") != NULL)
+  if (name0[0] == '<' || strstr (name0, "___") != NULL)
     {
       strncpy (name, name0, len);
       name[len] = '\000';
@@ -474,15 +473,6 @@ processId (const char *name0, int len)
 	  while (i0 < len && name0[i0] != '\'');
 	  i0 += 1;
 	  break;
-	case '<':
-	  i0 += 1;
-	  while (i0 < len && name0[i0] != '>')
-	    {
-	      name[i] = name0[i0];
-	      i += 1; i0 += 1;
-	    }
-	  i0 += 1;
-	  break;
 	}
     }
   name[i] = '\000';
diff --git a/gdb/block.c b/gdb/block.c
index 1c343aa..a8075a1 100644
--- a/gdb/block.c
+++ b/gdb/block.c
@@ -595,8 +595,7 @@ block_iterator_next (struct block_iterator *iterator)
 
 static struct symbol *
 block_iter_match_step (struct block_iterator *iterator,
-		       const char *name,
-		       symbol_compare_ftype *compare,
+		       const lookup_name_info &name,
 		       int first)
 {
   struct symbol *sym;
@@ -618,10 +617,10 @@ block_iter_match_step (struct block_iterator *iterator,
 	  block = BLOCKVECTOR_BLOCK (COMPUNIT_BLOCKVECTOR (cust),
 				     iterator->which);
 	  sym = dict_iter_match_first (BLOCK_DICT (block), name,
-				       compare, &iterator->dict_iter);
+				       &iterator->dict_iter);
 	}
       else
-	sym = dict_iter_match_next (name, compare, &iterator->dict_iter);
+	sym = dict_iter_match_next (name, &iterator->dict_iter);
 
       if (sym != NULL)
 	return sym;
@@ -638,30 +637,27 @@ block_iter_match_step (struct block_iterator *iterator,
 
 struct symbol *
 block_iter_match_first (const struct block *block,
-			const char *name,
-			symbol_compare_ftype *compare,
+			const lookup_name_info &name,
 			struct block_iterator *iterator)
 {
   initialize_block_iterator (block, iterator);
 
   if (iterator->which == FIRST_LOCAL_BLOCK)
-    return dict_iter_match_first (block->dict, name, compare,
-				  &iterator->dict_iter);
+    return dict_iter_match_first (block->dict, name, &iterator->dict_iter);
 
-  return block_iter_match_step (iterator, name, compare, 1);
+  return block_iter_match_step (iterator, name, 1);
 }
 
 /* See block.h.  */
 
 struct symbol *
-block_iter_match_next (const char *name,
-		       symbol_compare_ftype *compare,
+block_iter_match_next (const lookup_name_info &name,
 		       struct block_iterator *iterator)
 {
   if (iterator->which == FIRST_LOCAL_BLOCK)
-    return dict_iter_match_next (name, compare, &iterator->dict_iter);
+    return dict_iter_match_next (name, &iterator->dict_iter);
 
-  return block_iter_match_step (iterator, name, compare, 0);
+  return block_iter_match_step (iterator, name, 0);
 }
 
 /* See block.h.
@@ -682,11 +678,13 @@ block_lookup_symbol (const struct block *block, const char *name,
   struct block_iterator iter;
   struct symbol *sym;
 
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+
   if (!BLOCK_FUNCTION (block))
     {
       struct symbol *other = NULL;
 
-      ALL_BLOCK_SYMBOLS_WITH_NAME (block, name, iter, sym)
+      ALL_BLOCK_SYMBOLS_WITH_NAME (block, lookup_name, iter, sym)
 	{
 	  if (SYMBOL_DOMAIN (sym) == domain)
 	    return sym;
@@ -713,7 +711,7 @@ block_lookup_symbol (const struct block *block, const char *name,
 
       struct symbol *sym_found = NULL;
 
-      ALL_BLOCK_SYMBOLS_WITH_NAME (block, name, iter, sym)
+      ALL_BLOCK_SYMBOLS_WITH_NAME (block, lookup_name, iter, sym)
 	{
 	  if (symbol_matches_domain (SYMBOL_LANGUAGE (sym),
 				     SYMBOL_DOMAIN (sym), domain))
@@ -738,14 +736,16 @@ block_lookup_symbol_primary (const struct block *block, const char *name,
   struct symbol *sym, *other;
   struct dict_iterator dict_iter;
 
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+
   /* Verify BLOCK is STATIC_BLOCK or GLOBAL_BLOCK.  */
   gdb_assert (BLOCK_SUPERBLOCK (block) == NULL
 	      || BLOCK_SUPERBLOCK (BLOCK_SUPERBLOCK (block)) == NULL);
 
   other = NULL;
-  for (sym = dict_iter_match_first (block->dict, name, strcmp_iw, &dict_iter);
+  for (sym = dict_iter_match_first (block->dict, lookup_name, &dict_iter);
        sym != NULL;
-       sym = dict_iter_match_next (name, strcmp_iw, &dict_iter))
+       sym = dict_iter_match_next (lookup_name, &dict_iter))
     {
       if (SYMBOL_DOMAIN (sym) == domain)
 	return sym;
@@ -772,11 +772,13 @@ block_find_symbol (const struct block *block, const char *name,
   struct block_iterator iter;
   struct symbol *sym;
 
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+
   /* Verify BLOCK is STATIC_BLOCK or GLOBAL_BLOCK.  */
   gdb_assert (BLOCK_SUPERBLOCK (block) == NULL
 	      || BLOCK_SUPERBLOCK (BLOCK_SUPERBLOCK (block)) == NULL);
 
-  ALL_BLOCK_SYMBOLS_WITH_NAME (block, name, iter, sym)
+  ALL_BLOCK_SYMBOLS_WITH_NAME (block, lookup_name, iter, sym)
     {
       /* MATCHER is deliberately called second here so that it never sees
 	 a non-domain-matching symbol.  */
diff --git a/gdb/block.h b/gdb/block.h
index 1741e52..0326c18 100644
--- a/gdb/block.h
+++ b/gdb/block.h
@@ -237,27 +237,22 @@ extern struct symbol *block_iterator_first (const struct block *block,
 extern struct symbol *block_iterator_next (struct block_iterator *iterator);
 
 /* Initialize ITERATOR to point at the first symbol in BLOCK whose
-   SYMBOL_SEARCH_NAME is NAME, as tested using COMPARE (which must use
-   the same conventions as strcmp_iw and be compatible with any
-   block hashing function), and return that first symbol, or NULL
-   if there are no such symbols.  */
+   SYMBOL_SEARCH_NAME matches NAME, and return that first symbol, or
+   NULL if there are no such symbols.  */
 
 extern struct symbol *block_iter_match_first (const struct block *block,
-					      const char *name,
-					      symbol_compare_ftype *compare,
+					      const lookup_name_info &name,
 					      struct block_iterator *iterator);
 
 /* Advance ITERATOR to point at the next symbol in BLOCK whose
-   SYMBOL_SEARCH_NAME is NAME, as tested using COMPARE (see
-   block_iter_match_first), or NULL if there are no more such symbols.
-   Don't call this if you've previously received NULL from 
+   SYMBOL_SEARCH_NAME matches NAME, or NULL if there are no more such
+   symbols.  Don't call this if you've previously received NULL from
    block_iterator_match_first or block_iterator_match_next on this
    iteration.  And don't call it unless ITERATOR was created by a
-   previous call to block_iter_match_first with the same NAME and COMPARE.  */
+   previous call to block_iter_match_first with the same NAME.  */
 
-extern struct symbol *block_iter_match_next (const char *name,
-					     symbol_compare_ftype *compare,
-					     struct block_iterator *iterator);
+extern struct symbol *block_iter_match_next
+  (const lookup_name_info &name, struct block_iterator *iterator);
 
 /* Search BLOCK for symbol NAME in DOMAIN.  */
 
@@ -316,14 +311,14 @@ extern int block_find_non_opaque_type_preferred (struct symbol *sym,
        (sym);						\
        (sym) = block_iterator_next (&(iter)))
 
-/* Macro to loop through all symbols with name NAME in BLOCK,
-   in no particular order.  ITER helps keep track of the iteration, and
-   must be a struct block_iterator.  SYM points to the current symbol.  */
+/* Macro to loop through all symbols in BLOCK with a name that matches
+   NAME, in no particular order.  ITER helps keep track of the
+   iteration, and must be a struct block_iterator.  SYM points to the
+   current symbol.  */
 
 #define ALL_BLOCK_SYMBOLS_WITH_NAME(block, name, iter, sym)		\
-  for ((sym) = block_iter_match_first ((block), (name),			\
-				       strcmp_iw, &(iter));		\
+  for ((sym) = block_iter_match_first ((block), (name), &(iter));	\
        (sym) != NULL;							\
-       (sym) = block_iter_match_next ((name), strcmp_iw, &(iter)))
+       (sym) = block_iter_match_next ((name), &(iter)))
 
 #endif /* BLOCK_H */
diff --git a/gdb/c-lang.c b/gdb/c-lang.c
index 9749935..49077c7 100644
--- a/gdb/c-lang.c
+++ b/gdb/c-lang.c
@@ -869,7 +869,7 @@ extern const struct language_defn c_language_defn =
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &c_varobj_ops,
@@ -1014,7 +1014,7 @@ extern const struct language_defn cplus_language_defn =
   cp_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  cp_get_symbol_name_matcher,
   iterate_over_symbols,
   default_search_name_hash,
   &cplus_varobj_ops,
@@ -1068,7 +1068,7 @@ extern const struct language_defn asm_language_defn =
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
@@ -1122,7 +1122,7 @@ extern const struct language_defn minimal_language_defn =
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/completer.c b/gdb/completer.c
index cd0ecc3..68e9171 100644
--- a/gdb/completer.c
+++ b/gdb/completer.c
@@ -499,6 +499,7 @@ complete_files_symbols (completion_tracker &tracker,
     {
       collect_file_symbol_completion_matches (tracker,
 					      complete_symbol_mode::EXPRESSION,
+					      symbol_name_match_type::EXPRESSION,
 					      symbol_start, word,
 					      file_to_match);
       xfree (file_to_match);
@@ -509,6 +510,7 @@ complete_files_symbols (completion_tracker &tracker,
 
       collect_symbol_completion_matches (tracker,
 					 complete_symbol_mode::EXPRESSION,
+					 symbol_name_match_type::EXPRESSION,
 					 symbol_start, word);
       /* If text includes characters which cannot appear in a file
 	 name, they cannot be asking for completion on files.  */
@@ -551,6 +553,7 @@ complete_files_symbols (completion_tracker &tracker,
 	 on the entire text as a symbol.  */
       collect_symbol_completion_matches (tracker,
 					 complete_symbol_mode::EXPRESSION,
+					 symbol_name_match_type::EXPRESSION,
 					 orig_text, word);
     }
 }
@@ -1104,6 +1107,7 @@ symbol_completer (struct cmd_list_element *ignore,
 		  const char *text, const char *word)
 {
   collect_symbol_completion_matches (tracker, complete_symbol_mode::EXPRESSION,
+				     symbol_name_match_type::EXPRESSION,
 				     text, word);
 }
 
diff --git a/gdb/completer.h b/gdb/completer.h
index 82a994c..38fee6b 100644
--- a/gdb/completer.h
+++ b/gdb/completer.h
@@ -68,6 +68,62 @@ struct match_list_displayer
    calls free on each element.  */
 typedef std::vector<gdb::unique_xmalloc_ptr<char>> completion_list;
 
+/* The result of a successful completion match.  When doing symbol
+   comparison, we use the symbol search name for the symbol name match
+   check, but the matched name that is shown to the user may be
+   different.  For example, Ada uses encoded names for lookup, but
+   then wants to decode the symbol name to show to the user, and also
+   in some cases wrap the matched name in "<sym>" (meaning we can't
+   always use the symbol's print name).  */
+
+class completion_match
+{
+public:
+  /* Get the completion match result.  See m_match/m_storage's
+     descriptions.  */
+  const char *match ()
+  { return m_match; }
+
+  /* Set the completion match result.  See m_match/m_storage's
+     descriptions.  */
+  void set_match (const char *match)
+  { m_match = match; }
+
+  /* Get temporary storage for generating a match result, dynamically.
+     The built string is only good until the next clear() call.  I.e.,
+     good until the next symbol comparison.  */
+  std::string &storage ()
+  { return m_storage; }
+
+  /* Prepare for another completion matching sequence.  */
+  void clear ()
+  {
+    m_match = NULL;
+    m_storage.clear ();
+  }
+
+private:
+  /* The completion match result.  This can either be a pointer into
+     M_STORAGE string, or it can be a pointer into the some other
+     string that outlives the completion matching sequence (usually, a
+     pointer to a symbol's name).  */
+  const char *m_match;
+
+  /* Storage a symbol comparison routine can use for generating a
+     match result, dynamically.  The built string is only good until
+     the next clear() call.  I.e., good until the next symbol
+     comparison.  */
+  std::string m_storage;
+};
+
+/* Convenience aggregate holding info returned by the symbol name
+   matching routines (see symbol_name_matcher_ftype).  */
+struct completion_match_result
+{
+  /* The completion match candidate.  */
+  completion_match match;
+};
+
 /* The final result of a completion that is handed over to either
    readline or the "completion" command (which pretends to be
    readline).  Mainly a wrapper for a readline-style match list array,
@@ -203,6 +259,18 @@ public:
      already have.  */
   bool completes_to_completion_word (const char *word);
 
+  /* Get a reference to the shared (between all the multiple symbol
+     name comparison calls) completion_match_result object, ready for
+     another symbol name match sequence.  */
+  completion_match_result &reset_completion_match_result ()
+  {
+    completion_match_result &res = m_completion_match_result;
+
+    /* Clear any previous match.  */
+    res.match.clear ();
+    return m_completion_match_result;
+  }
+
   /* True if we have any completion match recorded.  */
   bool have_completions () const
   { return !m_entries_vec.empty (); }
@@ -228,6 +296,13 @@ private:
      to hand over to readline.  */
   void recompute_lowest_common_denominator (const char *new_match);
 
+  /* Completion match outputs returned by the symbol name matching
+     routines (see symbol_name_matcher_ftype).  These results are only
+     valid for a single match call.  This is here in order to be able
+     to conveniently share the same storage among all the calls to the
+     symbol name matching routines.  */
+  completion_match_result m_completion_match_result;
+
   /* The completion matches found so far, in a vector.  */
   completion_list m_entries_vec;
 
diff --git a/gdb/cp-support.c b/gdb/cp-support.c
index 6045cb0..defe509 100644
--- a/gdb/cp-support.c
+++ b/gdb/cp-support.c
@@ -1181,7 +1181,9 @@ make_symbol_overload_list_block (const char *name,
   struct block_iterator iter;
   struct symbol *sym;
 
-  ALL_BLOCK_SYMBOLS_WITH_NAME (block, name, iter, sym)
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+
+  ALL_BLOCK_SYMBOLS_WITH_NAME (block, lookup_name, iter, sym)
     overload_list_add_symbol (sym, name);
 }
 
@@ -1564,6 +1566,40 @@ gdb_sniff_from_mangled_name (const char *mangled, char **demangled)
   return *demangled != NULL;
 }
 
+/* C++ symbol_name_matcher_ftype implementation.  */
+
+static bool
+cp_fq_symbol_name_matches (const char *symbol_search_name,
+			   const lookup_name_info &lookup_name,
+			   completion_match *match)
+{
+  /* Get the demangled name.  */
+  const std::string &name = lookup_name.cplus ().lookup_name ();
+
+  strncmp_iw_mode mode = (lookup_name.completion_mode ()
+			  ? strncmp_iw_mode::NORMAL
+			  : strncmp_iw_mode::MATCH_PARAMS);
+
+  if (strncmp_iw_with_mode (symbol_search_name,
+			    name.c_str (), name.size (),
+			    mode) == 0)
+    {
+      if (match != NULL)
+	match->set_match (symbol_search_name);
+      return true;
+    }
+
+  return false;
+}
+
+/* See cp-support.h.  */
+
+symbol_name_matcher_ftype *
+cp_get_symbol_name_matcher (const lookup_name_info &lookup_name)
+{
+  return cp_fq_symbol_name_matches;
+}
+
 /* Don't allow just "maintenance cplus".  */
 
 static  void
diff --git a/gdb/cp-support.h b/gdb/cp-support.h
index 28353a2..a699a80 100644
--- a/gdb/cp-support.h
+++ b/gdb/cp-support.h
@@ -108,6 +108,11 @@ extern struct symbol **make_symbol_overload_list_adl (struct type **arg_types,
 extern struct type *cp_lookup_rtti_type (const char *name,
 					 struct block *block);
 
+/* Implement the "la_get_symbol_name_matcher" language_defn method for
+   C++.  */
+extern symbol_name_matcher_ftype *cp_get_symbol_name_matcher
+  (const lookup_name_info &lookup_name);
+
 /* Functions/variables from cp-namespace.c.  */
 
 extern int cp_is_in_anonymous (const char *symbol_name);
diff --git a/gdb/d-lang.c b/gdb/d-lang.c
index 2fa429b..e83e599 100644
--- a/gdb/d-lang.c
+++ b/gdb/d-lang.c
@@ -245,7 +245,7 @@ extern const struct language_defn d_language_defn =
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/dictionary.c b/gdb/dictionary.c
index 1ffa4f3..8277c5d 100644
--- a/gdb/dictionary.c
+++ b/gdb/dictionary.c
@@ -115,11 +115,9 @@ struct dict_vector
   struct symbol *(*iterator_next) (struct dict_iterator *iterator);
   /* Functions to iterate over symbols with a given name.  */
   struct symbol *(*iter_match_first) (const struct dictionary *dict,
-				      const char *name,
-				      symbol_compare_ftype *equiv,
+				      const lookup_name_info &name,
 				      struct dict_iterator *iterator);
-  struct symbol *(*iter_match_next) (const char *name,
-				     symbol_compare_ftype *equiv,
+  struct symbol *(*iter_match_next) (const lookup_name_info &name,
 				     struct dict_iterator *iterator);
   /* A size function, for maint print symtabs.  */
   int (*size) (const struct dictionary *dict);
@@ -239,12 +237,10 @@ static struct symbol *iterator_first_hashed (const struct dictionary *dict,
 static struct symbol *iterator_next_hashed (struct dict_iterator *iterator);
 
 static struct symbol *iter_match_first_hashed (const struct dictionary *dict,
-					       const char *name,
-					       symbol_compare_ftype *compare,
+					       const lookup_name_info &name,
 					      struct dict_iterator *iterator);
 
-static struct symbol *iter_match_next_hashed (const char *name,
-					      symbol_compare_ftype *compare,
+static struct symbol *iter_match_next_hashed (const lookup_name_info &name,
 					      struct dict_iterator *iterator);
 
 /* Functions only for DICT_HASHED.  */
@@ -269,12 +265,10 @@ static struct symbol *iterator_first_linear (const struct dictionary *dict,
 static struct symbol *iterator_next_linear (struct dict_iterator *iterator);
 
 static struct symbol *iter_match_first_linear (const struct dictionary *dict,
-					       const char *name,
-					       symbol_compare_ftype *compare,
+					       const lookup_name_info &name,
 					       struct dict_iterator *iterator);
 
-static struct symbol *iter_match_next_linear (const char *name,
-					      symbol_compare_ftype *compare,
+static struct symbol *iter_match_next_linear (const lookup_name_info &name,
 					      struct dict_iterator *iterator);
 
 static int size_linear (const struct dictionary *dict);
@@ -526,19 +520,18 @@ dict_iterator_next (struct dict_iterator *iterator)
 
 struct symbol *
 dict_iter_match_first (const struct dictionary *dict,
-		       const char *name, symbol_compare_ftype *compare,
+		       const lookup_name_info &name,
 		       struct dict_iterator *iterator)
 {
-  return (DICT_VECTOR (dict))->iter_match_first (dict, name,
-						 compare, iterator);
+  return (DICT_VECTOR (dict))->iter_match_first (dict, name, iterator);
 }
 
 struct symbol *
-dict_iter_match_next (const char *name, symbol_compare_ftype *compare,
+dict_iter_match_next (const lookup_name_info &name,
 		      struct dict_iterator *iterator)
 {
   return (DICT_VECTOR (DICT_ITERATOR_DICT (iterator)))
-    ->iter_match_next (name, compare, iterator);
+    ->iter_match_next (name, iterator);
 }
 
 int
@@ -629,13 +622,15 @@ iterator_hashed_advance (struct dict_iterator *iterator)
 }
 
 static struct symbol *
-iter_match_first_hashed (const struct dictionary *dict, const char *name,
-			 symbol_compare_ftype *compare,
+iter_match_first_hashed (const struct dictionary *dict,
+			 const lookup_name_info &name,
 			 struct dict_iterator *iterator)
 {
-  unsigned int hash_index
-    = (search_name_hash (DICT_LANGUAGE (dict)->la_language, name)
-       % DICT_HASHED_NBUCKETS (dict));
+  const language_defn *lang = DICT_LANGUAGE (dict);
+  unsigned int hash_index = (name.search_name_hash (lang->la_language)
+			     % DICT_HASHED_NBUCKETS (dict));
+  symbol_name_matcher_ftype *matches_name
+    = language_get_symbol_name_matcher (lang, name);
   struct symbol *sym;
 
   DICT_ITERATOR_DICT (iterator) = dict;
@@ -649,11 +644,8 @@ iter_match_first_hashed (const struct dictionary *dict, const char *name,
        sym = sym->hash_next)
     {
       /* Warning: the order of arguments to compare matters!  */
-      if (compare (SYMBOL_SEARCH_NAME (sym), name) == 0)
-	{
-	  break;
-	}
-	
+      if (matches_name (SYMBOL_SEARCH_NAME (sym), name, NULL))
+	break;
     }
 
   DICT_ITERATOR_CURRENT (iterator) = sym;
@@ -661,16 +653,19 @@ iter_match_first_hashed (const struct dictionary *dict, const char *name,
 }
 
 static struct symbol *
-iter_match_next_hashed (const char *name, symbol_compare_ftype *compare,
+iter_match_next_hashed (const lookup_name_info &name,
 			struct dict_iterator *iterator)
 {
+  const language_defn *lang = DICT_LANGUAGE (DICT_ITERATOR_DICT (iterator));
+  symbol_name_matcher_ftype *matches_name
+    = language_get_symbol_name_matcher (lang, name);
   struct symbol *next;
 
   for (next = DICT_ITERATOR_CURRENT (iterator)->hash_next;
        next != NULL;
        next = next->hash_next)
     {
-      if (compare (SYMBOL_SEARCH_NAME (next), name) == 0)
+      if (matches_name (SYMBOL_SEARCH_NAME (next), name, NULL))
 	break;
     }
 
@@ -863,27 +858,32 @@ iterator_next_linear (struct dict_iterator *iterator)
 
 static struct symbol *
 iter_match_first_linear (const struct dictionary *dict,
-			 const char *name, symbol_compare_ftype *compare,
+			 const lookup_name_info &name,
 			 struct dict_iterator *iterator)
 {
   DICT_ITERATOR_DICT (iterator) = dict;
   DICT_ITERATOR_INDEX (iterator) = -1;
 
-  return iter_match_next_linear (name, compare, iterator);
+  return iter_match_next_linear (name, iterator);
 }
 
 static struct symbol *
-iter_match_next_linear (const char *name, symbol_compare_ftype *compare,
+iter_match_next_linear (const lookup_name_info &name,
 			struct dict_iterator *iterator)
 {
   const struct dictionary *dict = DICT_ITERATOR_DICT (iterator);
+  const language_defn *lang = DICT_LANGUAGE (dict);
+  symbol_name_matcher_ftype *matches_name
+    = language_get_symbol_name_matcher (lang, name);
+
   int i, nsyms = DICT_LINEAR_NSYMS (dict);
   struct symbol *sym, *retval = NULL;
 
   for (i = DICT_ITERATOR_INDEX (iterator) + 1; i < nsyms; ++i)
     {
       sym = DICT_LINEAR_SYM (dict, i);
-      if (compare (SYMBOL_SEARCH_NAME (sym), name) == 0)
+
+      if (matches_name (SYMBOL_SEARCH_NAME (sym), name, NULL))
 	{
 	  retval = sym;
 	  break;
diff --git a/gdb/dictionary.h b/gdb/dictionary.h
index e4a9315..e65026b 100644
--- a/gdb/dictionary.h
+++ b/gdb/dictionary.h
@@ -133,8 +133,7 @@ extern struct symbol *dict_iterator_next (struct dict_iterator *iterator);
    if there are no such symbols.  */
 
 extern struct symbol *dict_iter_match_first (const struct dictionary *dict,
-					     const char *name,
-					     symbol_compare_ftype *compare,
+					     const lookup_name_info &name,
 					     struct dict_iterator *iterator);
 
 /* Advance ITERATOR to point at the next symbol in DICT whose
@@ -145,8 +144,7 @@ extern struct symbol *dict_iter_match_first (const struct dictionary *dict,
    iteration.  And don't call it unless ITERATOR was created by a
    previous call to dict_iter_match_first with the same NAME and COMPARE.  */
 
-extern struct symbol *dict_iter_match_next (const char *name,
-					    symbol_compare_ftype *compare,
+extern struct symbol *dict_iter_match_next (const lookup_name_info &name,
 					    struct dict_iterator *iterator);
 
 /* Return some notion of the size of the dictionary: the number of
diff --git a/gdb/dwarf2read.c b/gdb/dwarf2read.c
index 544d1e4..f37d51f 100644
--- a/gdb/dwarf2read.c
+++ b/gdb/dwarf2read.c
@@ -3878,6 +3878,8 @@ dw2_lookup_symbol (struct objfile *objfile, int block_index,
 
   dw2_setup (objfile);
 
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+
   index = dwarf2_per_objfile->index_table;
 
   /* index is NULL if OBJF_READNOW.  */
@@ -3904,10 +3906,10 @@ dw2_lookup_symbol (struct objfile *objfile, int block_index,
 	     information (but NAME might contain it).  */
 
 	  if (sym != NULL
-	      && SYMBOL_MATCHES_SEARCH_NAME (sym, name))
+	      && SYMBOL_MATCHES_SEARCH_NAME (sym, lookup_name))
 	    return stab;
 	  if (with_opaque != NULL
-	      && SYMBOL_MATCHES_SEARCH_NAME (with_opaque, name))
+	      && SYMBOL_MATCHES_SEARCH_NAME (with_opaque, lookup_name))
 	    stab_best = stab;
 
 	  /* Keep looking through other CUs.  */
@@ -4052,7 +4054,7 @@ dw2_map_matching_symbols (struct objfile *objfile,
 			  int global,
 			  int (*callback) (struct block *,
 					   struct symbol *, void *),
-			  void *data, symbol_compare_ftype *match,
+			  void *data, symbol_name_match_type match,
 			  symbol_compare_ftype *ordered_compare)
 {
   /* Currently unimplemented; used for Ada.  The function can be called if the
@@ -4060,10 +4062,96 @@ dw2_map_matching_symbols (struct objfile *objfile,
      does not look for non-Ada symbols this function should just return.  */
 }
 
+/* Symbol name matcher for .gdb_index names.
+
+   Symbol names in .gdb_index have a few particularities:
+
+   - There's no indication of which is the language of each symbol.
+
+     Since each language has its own symbol name matching algorithm,
+     and we don't know which language is the right one, we must match
+     each symbol against all languages.
+
+   - Symbol names in the index have no overload (parameter)
+     information.  I.e., in C++, "foo(int)" and "foo(long)" both
+     appear as "foo" in the index, for example.
+
+     This means that the lookup names passed to the symbol name
+     matcher functions must have no parameter information either
+     because (e.g.) symbol search name "foo" does not match
+     lookup-name "foo(int)" [while swapping search name for lookup
+     name would match].
+*/
+class gdb_index_symbol_name_matcher
+{
+public:
+  /* Prepares the vector of comparison functions for LOOKUP_NAME.  */
+  gdb_index_symbol_name_matcher (const lookup_name_info &lookup_name);
+
+  /* Walk all the matcher routines and match SYMBOL_NAME against them.
+     Returns true if any matcher matches.  */
+  bool matches (const char *symbol_name);
+
+private:
+  /* A reference to the lookup name we're matching against.  */
+  const lookup_name_info &m_lookup_name;
+
+  /* A vector holding all the different symbol name matchers, for all
+     languages.  */
+  std::vector<symbol_name_matcher_ftype *> m_symbol_name_matcher_funcs;
+};
+
+gdb_index_symbol_name_matcher::gdb_index_symbol_name_matcher
+  (const lookup_name_info &lookup_name)
+    : m_lookup_name (lookup_name)
+{
+  /* Prepare the vector of comparison functions upfront, to avoid
+     doing the same work for each symbol.  Care is taken to avoid
+     matching with the same matcher more than once if/when multiple
+     languages use the same matcher function.  */
+  auto &matchers = m_symbol_name_matcher_funcs;
+  matchers.reserve (nr_languages);
+
+  matchers.push_back (default_symbol_name_matcher);
+
+  for (int i = 0; i < nr_languages; i++)
+    {
+      const language_defn *lang = language_def ((enum language) i);
+      if (lang->la_get_symbol_name_matcher != NULL)
+	{
+	  symbol_name_matcher_ftype *name_matcher
+	    = lang->la_get_symbol_name_matcher (m_lookup_name);
+
+	  /* Don't insert the same comparison routine more than once.
+	     Note that we do this linear walk instead of a cheaper
+	     sorted insert, or use a std::set or something like that,
+	     because relative order of function addresses is not
+	     stable.  This is not a problem in practice because the
+	     number of supported languages is low, and the cost here
+	     is tiny compared to the number of searches we'll do
+	     afterwards using this object.  */
+	  if (std::find (matchers.begin (), matchers.end (), name_matcher)
+	      == matchers.end ())
+	    matchers.push_back (name_matcher);
+	}
+    }
+}
+
+bool
+gdb_index_symbol_name_matcher::matches (const char *symbol_name)
+{
+  for (auto matches_name : m_symbol_name_matcher_funcs)
+    if (matches_name (symbol_name, m_lookup_name, NULL))
+      return true;
+
+  return false;
+}
+
 static void
 dw2_expand_symtabs_matching
   (struct objfile *objfile,
    gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   const lookup_name_info &lookup_name,
    gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
    gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
    enum search_domain kind)
@@ -4151,6 +4239,8 @@ dw2_expand_symtabs_matching
 	}
     }
 
+  gdb_index_symbol_name_matcher lookup_name_matcher (lookup_name);
+
   for (iter = 0; iter < index->symbol_table_slots; ++iter)
     {
       offset_type idx = 2 * iter;
@@ -4165,7 +4255,8 @@ dw2_expand_symtabs_matching
 
       name = index->constant_pool + MAYBE_SWAP (index->symbol_table[idx]);
 
-      if (!symbol_matcher (name))
+      if (!lookup_name_matcher.matches (name)
+	  || (symbol_matcher != NULL && !symbol_matcher (name)))
 	continue;
 
       /* The name was matched, now expand corresponding CUs that were
diff --git a/gdb/f-lang.c b/gdb/f-lang.c
index 0d78e5a..fe909ff 100644
--- a/gdb/f-lang.c
+++ b/gdb/f-lang.c
@@ -229,10 +229,12 @@ f_word_break_characters (void)
 static void
 f_collect_symbol_completion_matches (completion_tracker &tracker,
 				     complete_symbol_mode mode,
+				     symbol_name_match_type compare_name,
 				     const char *text, const char *word,
 				     enum type_code code)
 {
   default_collect_symbol_completion_matches_break_on (tracker, mode,
+						      compare_name,
 						      text, word, ":", code);
 }
 
@@ -289,7 +291,7 @@ extern const struct language_defn f_language_defn =
   default_pass_by_reference,
   default_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/go-lang.c b/gdb/go-lang.c
index 87ad063..9f88e32 100644
--- a/gdb/go-lang.c
+++ b/gdb/go-lang.c
@@ -606,7 +606,7 @@ extern const struct language_defn go_language_defn =
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/language.c b/gdb/language.c
index a53119c..76047c7 100644
--- a/gdb/language.c
+++ b/gdb/language.c
@@ -699,6 +699,41 @@ default_get_string (struct value *value, gdb_byte **buffer, int *length,
   error (_("Getting a string is unsupported in this language."));
 }
 
+/* See language.h.  */
+
+bool
+default_symbol_name_matcher (const char *symbol_search_name,
+			     const lookup_name_info &lookup_name,
+			     completion_match *match)
+{
+  const std::string &name = lookup_name.name ();
+
+  strncmp_iw_mode mode = (lookup_name.completion_mode ()
+			  ? strncmp_iw_mode::NORMAL
+			  : strncmp_iw_mode::MATCH_PARAMS);
+
+  if (strncmp_iw_with_mode (symbol_search_name, name.c_str (), name.size (),
+			    mode) == 0)
+    {
+      if (match != NULL)
+	match->set_match (symbol_search_name);
+      return true;
+    }
+  else
+    return false;
+}
+
+/* See language.h.  */
+
+symbol_name_matcher_ftype *
+language_get_symbol_name_matcher (const language_defn *lang,
+				  const lookup_name_info &lookup_name)
+{
+  if (lang->la_get_symbol_name_matcher != nullptr)
+    return lang->la_get_symbol_name_matcher (lookup_name);
+  return default_symbol_name_matcher;
+}
+
 /* Define the language that is no language.  */
 
 static int
@@ -837,7 +872,7 @@ const struct language_defn unknown_language_defn =
   default_pass_by_reference,
   default_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
@@ -888,7 +923,7 @@ const struct language_defn auto_language_defn =
   default_pass_by_reference,
   default_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/language.h b/gdb/language.h
index 64ccfcc..34f5692 100644
--- a/gdb/language.h
+++ b/gdb/language.h
@@ -126,16 +126,6 @@ struct language_arch_info
   struct type *bool_type_default;
 };
 
-/* A pointer to a function expected to return nonzero if
-   SYMBOL_SEARCH_NAME matches the given LOOKUP_NAME.
-
-   SYMBOL_SEARCH_NAME should be a symbol's "search" name.
-   LOOKUP_NAME should be the name of an entity after it has been
-   transformed for lookup.  */
-
-typedef int (*symbol_name_cmp_ftype) (const char *symbol_search_name,
-				      const char *lookup_name);
-
 /* Structure tying together assorted information about a language.  */
 
 struct language_defn
@@ -331,6 +321,7 @@ struct language_defn
     void (*la_collect_symbol_completion_matches)
       (completion_tracker &tracker,
        complete_symbol_mode mode,
+       symbol_name_match_type match_type,
        const char *text,
        const char *word,
        enum type_code code);
@@ -367,13 +358,18 @@ struct language_defn
     gdb::unique_xmalloc_ptr<char> (*la_watch_location_expression)
          (struct type *type, CORE_ADDR addr);
 
-    /* Return a pointer to the function that should be used to match
-       a symbol name against LOOKUP_NAME. This is mostly for languages
-       such as Ada where the matching algorithm depends on LOOKUP_NAME.
+    /* Return a pointer to the function that should be used to match a
+       symbol name against LOOKUP_NAME, according to this language's
+       rules.  The matching algorithm depends on LOOKUP_NAME.  For
+       example, on Ada, the matching algorithm depends on the symbol
+       name (wild/full/verbatim matching), and on whether we're doing
+       a normal lookup or a completion match lookup.
 
-       This field may be NULL, in which case strcmp_iw will be used
-       to perform the matching.  */
-    symbol_name_cmp_ftype (*la_get_symbol_name_cmp) (const char *lookup_name);
+       This field may be NULL, in which case
+       default_symbol_name_matcher is used to perform the
+       matching.  */
+    symbol_name_matcher_ftype *(*la_get_symbol_name_matcher)
+      (const lookup_name_info &);
 
     /* Find all symbols in the current program space matching NAME in
        DOMAIN, according to this language's rules.
@@ -389,7 +385,8 @@ struct language_defn
        special processing here, 'iterate_over_symbols' should be
        used as the definition.  */
     void (*la_iterate_over_symbols)
-      (const struct block *block, const char *name, domain_enum domain,
+      (const struct block *block, const lookup_name_info &name,
+       domain_enum domain,
        gdb::function_view<symbol_found_callback_ftype> callback);
 
     /* Hash the given symbol search name.  Use
@@ -627,6 +624,18 @@ extern unsigned int default_search_name_hash (const char *search_name);
 void c_get_string (struct value *value, gdb_byte **buffer, int *length,
 		   struct type **char_type, const char **charset);
 
+/* The default implementation of la_symbol_name_matcher.  Matches with
+   strncmp_iw.  */
+extern bool default_symbol_name_matcher
+  (const char *symbol_search_name,
+   const lookup_name_info &lookup_name,
+   completion_match *match);
+
+/* Get LANG's symbol_name_matcher method for LOOKUP_NAME.  Returns
+   default_symbol_name_matcher if not set.  */
+symbol_name_matcher_ftype *language_get_symbol_name_matcher
+  (const language_defn *lang, const lookup_name_info &lookup_name);
+
 /* The languages supported by GDB.  */
 
 extern const struct language_defn auto_language_defn;
diff --git a/gdb/linespec.c b/gdb/linespec.c
index 19db83e..05218bd 100644
--- a/gdb/linespec.c
+++ b/gdb/linespec.c
@@ -334,7 +334,8 @@ typedef struct ls_parser linespec_parser;
 /* Prototypes for local functions.  */
 
 static void iterate_over_file_blocks
-  (struct symtab *symtab, const char *name, domain_enum domain,
+  (struct symtab *symtab, const lookup_name_info &name,
+   domain_enum domain,
    gdb::function_view<symbol_found_callback_ftype> callback);
 
 static void initialize_defaults (struct symtab **default_symtab,
@@ -369,6 +370,7 @@ static int symbol_to_sal (struct symtab_and_line *result,
 			  int funfirstline, struct symbol *sym);
 
 static void add_matching_symbols_to_info (const char *name,
+					  symbol_name_match_type name_match_type,
 					  struct collect_info *info,
 					  struct program_space *pspace);
 
@@ -1101,19 +1103,15 @@ maybe_add_address (htab_t set, struct program_space *pspace, CORE_ADDR addr)
 
 static void
 iterate_over_all_matching_symtabs
-  (struct linespec_state *state, const char *name, const domain_enum domain,
+  (struct linespec_state *state,
+   const lookup_name_info &lookup_name,
+   const domain_enum name_domain,
    struct program_space *search_pspace, bool include_inline,
    gdb::function_view<symbol_found_callback_ftype> callback)
 {
   struct objfile *objfile;
   struct program_space *pspace;
 
-  /* The routine to be used for comparison.  */
-  symbol_name_cmp_ftype symbol_name_cmp
-    = (state->language->la_get_symbol_name_cmp != NULL
-       ? state->language->la_get_symbol_name_cmp (name)
-       : strcmp_iw);
-
   ALL_PSPACES (pspace)
   {
     if (search_pspace != NULL && search_pspace != pspace)
@@ -1128,21 +1126,17 @@ iterate_over_all_matching_symtabs
       struct compunit_symtab *cu;
 
       if (objfile->sf)
-	objfile->sf->qf->expand_symtabs_matching
-	  (objfile,
-	   NULL,
-	   [&] (const char *symbol_name)
-	   {
-	     return symbol_name_cmp (symbol_name, name) == 0;
-	   },
-	   NULL,
-	   ALL_DOMAIN);
+	objfile->sf->qf->expand_symtabs_matching (objfile,
+						  NULL,
+						  lookup_name,
+						  NULL, NULL,
+						  ALL_DOMAIN);
 
       ALL_OBJFILE_COMPUNITS (objfile, cu)
 	{
 	  struct symtab *symtab = COMPUNIT_FILETABS (cu);
 
-	  iterate_over_file_blocks (symtab, name, domain, callback);
+	  iterate_over_file_blocks (symtab, lookup_name, name_domain, callback);
 
 	  if (include_inline)
 	    {
@@ -1155,7 +1149,7 @@ iterate_over_all_matching_symtabs
 		{
 		  block = BLOCKVECTOR_BLOCK (SYMTAB_BLOCKVECTOR (symtab), i);
 		  state->language->la_iterate_over_symbols
-		    (block, name, domain, [&] (symbol *sym)
+		    (block, lookup_name, name_domain, [&] (symbol *sym)
 		     {
 		       /* Restrict calls to CALLBACK to symbols
 			  representing inline symbols only.  */
@@ -1192,8 +1186,8 @@ get_current_search_block (void)
 
 static void
 iterate_over_file_blocks
-  (struct symtab *symtab, const char *name, domain_enum domain,
-   gdb::function_view<symbol_found_callback_ftype> callback)
+  (struct symtab *symtab, const lookup_name_info &name,
+   domain_enum domain, gdb::function_view<symbol_found_callback_ftype> callback)
 {
   struct block *block;
 
@@ -1203,12 +1197,12 @@ iterate_over_file_blocks
     LA_ITERATE_OVER_SYMBOLS (block, name, domain, callback);
 }
 
-/* A helper for find_method.  This finds all methods in type T which
-   match NAME.  It adds matching symbol names to RESULT_NAMES, and
-   adds T's direct superclasses to SUPERCLASSES.  */
+/* A helper for find_method.  This finds all methods in type T of
+   language T_LANG which match NAME.  It adds matching symbol names to
+   RESULT_NAMES, and adds T's direct superclasses to SUPERCLASSES.  */
 
 static void
-find_methods (struct type *t, const char *name,
+find_methods (struct type *t, enum language t_lang, const char *name,
 	      VEC (const_char_ptr) **result_names,
 	      VEC (typep) **superclasses)
 {
@@ -1221,6 +1215,9 @@ find_methods (struct type *t, const char *name,
   if (class_name)
     {
       int method_counter;
+      lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+      symbol_name_matcher_ftype *symbol_name_compare
+	= language_get_symbol_name_matcher (language_def (t_lang), lookup_name);
 
       t = check_typedef (t);
 
@@ -1245,7 +1242,7 @@ find_methods (struct type *t, const char *name,
 		method_name = dem_opname;
 	    }
 
-	  if (strcmp_iw (method_name, name) == 0)
+	  if (symbol_name_compare (method_name, lookup_name, NULL))
 	    {
 	      int field_counter;
 
@@ -2831,15 +2828,19 @@ linespec_complete_function (completion_tracker &tracker,
 			    const char *source_filename)
 {
   complete_symbol_mode mode = complete_symbol_mode::LINESPEC;
+  symbol_name_match_type func_match_type = symbol_name_match_type::WILD;
 
   if (source_filename != NULL)
     {
-      collect_file_symbol_completion_matches (tracker, mode,
-					      function, function,
-					      source_filename);
+      collect_file_symbol_completion_matches (tracker, mode, func_match_type,
+					      function, function, source_filename);
     }
   else
-    collect_symbol_completion_matches (tracker, mode, function, function);
+    {
+      collect_symbol_completion_matches (tracker, mode, func_match_type,
+					 function, function);
+
+    }
 }
 
 /* Helper for complete_linespec to simplify it.  SOURCE_FILENAME is
@@ -3576,14 +3577,18 @@ lookup_prefix_sym (struct linespec_state *state, VEC (symtab_ptr) *file_symtabs,
   struct symtab *elt;
   decode_compound_collector collector;
 
+  lookup_name_info lookup_name (class_name, symbol_name_match_type::FULL);
+
   for (ix = 0; VEC_iterate (symtab_ptr, file_symtabs, ix, elt); ++ix)
     {
       if (elt == NULL)
 	{
-	  iterate_over_all_matching_symtabs (state, class_name, STRUCT_DOMAIN,
-					     NULL, false, collector);
-	  iterate_over_all_matching_symtabs (state, class_name, VAR_DOMAIN,
-					     NULL, false, collector);
+	  iterate_over_all_matching_symtabs (state, lookup_name,
+					     STRUCT_DOMAIN, NULL, false,
+					     collector);
+	  iterate_over_all_matching_symtabs (state, lookup_name,
+					     VAR_DOMAIN, NULL, false,
+					     collector);
 	}
       else
 	{
@@ -3591,8 +3596,8 @@ lookup_prefix_sym (struct linespec_state *state, VEC (symtab_ptr) *file_symtabs,
 	     been filtered out earlier.  */
 	  gdb_assert (!SYMTAB_PSPACE (elt)->executing_startup);
 	  set_current_program_space (SYMTAB_PSPACE (elt));
-	  iterate_over_file_blocks (elt, class_name, STRUCT_DOMAIN, collector);
-	  iterate_over_file_blocks (elt, class_name, VAR_DOMAIN, collector);
+	  iterate_over_file_blocks (elt, lookup_name, STRUCT_DOMAIN, collector);
+	  iterate_over_file_blocks (elt, lookup_name, VAR_DOMAIN, collector);
 	}
     }
 
@@ -3673,12 +3678,14 @@ add_all_symbol_names_from_pspace (struct collect_info *info,
   const char *iter;
 
   for (ix = 0; VEC_iterate (const_char_ptr, names, ix, iter); ++ix)
-    add_matching_symbols_to_info (iter, info, pspace);
+    add_matching_symbols_to_info (iter,
+				  symbol_name_match_type::FULL,
+				  info, pspace);
 }
 
 static void
 find_superclass_methods (VEC (typep) *superclasses,
-			 const char *name,
+			 const char *name, enum language name_lang,
 			 VEC (const_char_ptr) **result_names)
 {
   int old_len = VEC_length (const_char_ptr, *result_names);
@@ -3694,7 +3701,7 @@ find_superclass_methods (VEC (typep) *superclasses,
 
       make_cleanup (VEC_cleanup (typep), &new_supers);
       for (ix = 0; VEC_iterate (typep, iter_classes, ix, t); ++ix)
-	find_methods (t, name, result_names, &new_supers);
+	find_methods (t, name_lang, name, result_names, &new_supers);
 
       if (VEC_length (const_char_ptr, *result_names) != old_len
 	  || VEC_empty (typep, new_supers))
@@ -3761,7 +3768,8 @@ find_method (struct linespec_state *self, VEC (symtab_ptr) *file_symtabs,
       gdb_assert (!pspace->executing_startup);
       set_current_program_space (pspace);
       t = check_typedef (SYMBOL_TYPE (sym));
-      find_methods (t, method_name, &result_names, &superclass_vec);
+      find_methods (t, SYMBOL_LANGUAGE (sym),
+		    method_name, &result_names, &superclass_vec);
 
       /* Handle all items from a single program space at once; and be
 	 sure not to miss the last batch.  */
@@ -3774,7 +3782,7 @@ find_method (struct linespec_state *self, VEC (symtab_ptr) *file_symtabs,
 	     this program space, consider superclasses.  */
 	  if (VEC_length (const_char_ptr, result_names) == last_result_len)
 	    find_superclass_methods (superclass_vec, method_name,
-				     &result_names);
+				     SYMBOL_LANGUAGE (sym), &result_names);
 
 	  /* We have a list of candidate symbol names, so now we
 	     iterate over the symbol tables looking for all
@@ -3941,7 +3949,8 @@ find_function_symbols (struct linespec_state *state,
     add_all_symbol_names_from_pspace (&info, state->search_pspace,
 				      symbol_names);
   else
-    add_matching_symbols_to_info (name, &info, state->search_pspace);
+    add_matching_symbols_to_info (name, symbol_name_match_type::WILD,
+				  &info, state->search_pspace);
 
   do_cleanups (cleanup);
 
@@ -3968,28 +3977,10 @@ find_function_symbols (struct linespec_state *state,
 static void
 find_linespec_symbols (struct linespec_state *state,
 		       VEC (symtab_ptr) *file_symtabs,
-		       const char *name,
+		       const char *lookup_name,
 		       VEC (symbolp) **symbols,
 		       VEC (bound_minimal_symbol_d) **minsyms)
 {
-  demangle_result_storage demangle_storage;
-  std::string ada_lookup_storage;
-  const char *lookup_name;
-
-  if (state->language->la_language == language_ada)
-    {
-      /* In Ada, the symbol lookups are performed using the encoded
-         name rather than the demangled name.  */
-      ada_lookup_storage = ada_name_for_lookup (name);
-      lookup_name = ada_lookup_storage.c_str ();
-    }
-  else
-    {
-      lookup_name = demangle_for_lookup (name,
-					 state->language->la_language,
-					 demangle_storage);
-    }
-
   std::string canon = cp_canonicalize_string_no_typedefs (lookup_name);
   if (!canon.empty ())
     lookup_name = canon.c_str ();
@@ -4483,7 +4474,8 @@ add_minsym (struct minimal_symbol *minsym, void *d)
    restrict results to the given SYMTAB.  */
 
 static void
-search_minsyms_for_name (struct collect_info *info, const char *name,
+search_minsyms_for_name (struct collect_info *info,
+			 const lookup_name_info &name,
 			 struct program_space *search_pspace,
 			 struct symtab *symtab)
 {
@@ -4525,8 +4517,7 @@ search_minsyms_for_name (struct collect_info *info, const char *name,
 	{
 	  set_current_program_space (SYMTAB_PSPACE (symtab));
 	  local.objfile = SYMTAB_OBJFILE(symtab);
-	  iterate_over_minimal_symbols (local.objfile, name, add_minsym,
-					&local);
+	  iterate_over_minimal_symbols (local.objfile, name, add_minsym, &local);
 	}
     }
 
@@ -4568,20 +4559,24 @@ search_minsyms_for_name (struct collect_info *info, const char *name,
 
 static void
 add_matching_symbols_to_info (const char *name,
+			      symbol_name_match_type name_match_type,
 			      struct collect_info *info,
 			      struct program_space *pspace)
 {
   int ix;
   struct symtab *elt;
 
+  lookup_name_info lookup_name (name, name_match_type);
+
   for (ix = 0; VEC_iterate (symtab_ptr, info->file_symtabs, ix, elt); ++ix)
     {
       if (elt == NULL)
 	{
-	  iterate_over_all_matching_symtabs (info->state, name, VAR_DOMAIN,
+	  iterate_over_all_matching_symtabs (info->state, lookup_name,
+					     VAR_DOMAIN,
 					     pspace, true, [&] (symbol *sym)
 	    { return info->add_symbol (sym); });
-	  search_minsyms_for_name (info, name, pspace, NULL);
+	  search_minsyms_for_name (info, lookup_name, pspace, NULL);
 	}
       else if (pspace == NULL || pspace == SYMTAB_PSPACE (elt))
 	{
@@ -4591,7 +4586,8 @@ add_matching_symbols_to_info (const char *name,
 	     been filtered out earlier.  */
 	  gdb_assert (!SYMTAB_PSPACE (elt)->executing_startup);
 	  set_current_program_space (SYMTAB_PSPACE (elt));
-	  iterate_over_file_blocks (elt, name, VAR_DOMAIN, [&] (symbol *sym)
+	  iterate_over_file_blocks (elt, lookup_name, VAR_DOMAIN,
+				    [&] (symbol *sym)
 	    { return info->add_symbol (sym); });
 
 	  /* If no new symbols were found in this iteration and this symtab
@@ -4600,7 +4596,7 @@ add_matching_symbols_to_info (const char *name,
 	     this case.  */
 	  if (prev_len == VEC_length (symbolp, info->result.symbols)
 	      && elt->language == language_asm)
-	    search_minsyms_for_name (info, name, pspace, elt);
+	    search_minsyms_for_name (info, lookup_name, pspace, elt);
 	}
     }
 }
diff --git a/gdb/m2-lang.c b/gdb/m2-lang.c
index 4170ae9..09050c0 100644
--- a/gdb/m2-lang.c
+++ b/gdb/m2-lang.c
@@ -393,7 +393,7 @@ extern const struct language_defn m2_language_defn =
   default_pass_by_reference,
   default_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/minsyms.c b/gdb/minsyms.c
index 37edbd8..a0d3bd5 100644
--- a/gdb/minsyms.c
+++ b/gdb/minsyms.c
@@ -51,6 +51,7 @@
 #include "language.h"
 #include "cli/cli-utils.h"
 #include "symbol.h"
+#include <algorithm>
 
 /* See minsyms.h.  */
 
@@ -131,15 +132,139 @@ add_minsym_to_hash_table (struct minimal_symbol *sym,
    TABLE.  */
 static void
 add_minsym_to_demangled_hash_table (struct minimal_symbol *sym,
-                                  struct minimal_symbol **table)
+				    struct objfile *objfile)
 {
   if (sym->demangled_hash_next == NULL)
     {
-      unsigned int hash = msymbol_hash_iw (MSYMBOL_SEARCH_NAME (sym))
-	% MINIMAL_SYMBOL_HASH_SIZE;
+      unsigned int hash = search_name_hash (MSYMBOL_LANGUAGE (sym),
+					    MSYMBOL_SEARCH_NAME (sym));
+
+      auto &vec = objfile->per_bfd->demangled_hash_languages;
+      auto it = std::lower_bound (vec.begin (), vec.end (),
+				  MSYMBOL_LANGUAGE (sym));
+      if (it == vec.end () || *it != MSYMBOL_LANGUAGE (sym))
+	vec.insert (it, MSYMBOL_LANGUAGE (sym));
+
+      struct minimal_symbol **table
+	= objfile->per_bfd->msymbol_demangled_hash;
+      unsigned int hash_index = hash % MINIMAL_SYMBOL_HASH_SIZE;
+      sym->demangled_hash_next = table[hash_index];
+      table[hash_index] = sym;
+    }
+}
 
-      sym->demangled_hash_next = table[hash];
-      table[hash] = sym;
+/* Worker object for lookup_minimal_symbol.  Stores temporary results
+   while walking the symbol tables.  */
+
+struct found_minimal_symbols
+{
+  /* External symbols are best.  */
+  bound_minimal_symbol external_symbol {};
+
+  /* File-local symbols are next best.  */
+  bound_minimal_symbol file_symbol {};
+
+  /* Symbols for shared library trampolines are next best.  */
+  bound_minimal_symbol trampoline_symbol {};
+
+  /* Called when a symbol name matches.  Check if the minsym is a
+     better type than what we had already found, and record it in one
+     of the members fields if so.  Returns true if we collected the
+     real symbol, in which case we can stop searching.  */
+  bool maybe_collect (const char *sfile, objfile *objf,
+		      minimal_symbol *msymbol);
+};
+
+/* See declaration above.  */
+
+bool
+found_minimal_symbols::maybe_collect (const char *sfile,
+				      struct objfile *objfile,
+				      minimal_symbol *msymbol)
+{
+  switch (MSYMBOL_TYPE (msymbol))
+    {
+    case mst_file_text:
+    case mst_file_data:
+    case mst_file_bss:
+      if (sfile == NULL
+	  || filename_cmp (msymbol->filename, sfile) == 0)
+	{
+	  file_symbol.minsym = msymbol;
+	  file_symbol.objfile = objfile;
+	}
+      break;
+
+    case mst_solib_trampoline:
+
+      /* If a trampoline symbol is found, we prefer to keep
+	 looking for the *real* symbol.  If the actual symbol
+	 is not found, then we'll use the trampoline
+	 entry.  */
+      if (trampoline_symbol.minsym == NULL)
+	{
+	  trampoline_symbol.minsym = msymbol;
+	  trampoline_symbol.objfile = objfile;
+	}
+      break;
+
+    case mst_unknown:
+    default:
+      external_symbol.minsym = msymbol;
+      external_symbol.objfile = objfile;
+      /* We have the real symbol.  No use looking further.  */
+      return true;
+    }
+
+  /* Keep looking.  */
+  return false;
+}
+
+/* Walk the mangled name hash table, and pass each symbol whose name
+   matches LOOKUP_NAME according to NAMECMP to FOUND.  */
+
+static void
+lookup_minimal_symbol_mangled (const char *lookup_name,
+			       const char *sfile,
+			       struct objfile *objfile,
+			       struct minimal_symbol **table,
+			       unsigned int hash,
+			       int (*namecmp) (const char *, const char *),
+			       found_minimal_symbols &found)
+{
+  for (minimal_symbol *msymbol = table[hash];
+       msymbol != NULL;
+       msymbol = msymbol->hash_next)
+    {
+      const char *symbol_name = MSYMBOL_LINKAGE_NAME (msymbol);
+
+      if (namecmp (symbol_name, lookup_name) == 0
+	  && found.maybe_collect (sfile, objfile, msymbol))
+	return;
+    }
+}
+
+/* Walk the demangled name hash table, and pass each symbol whose name
+   matches LOOKUP_NAME according to MATCHER to FOUND.  */
+
+static void
+lookup_minimal_symbol_demangled (const lookup_name_info &lookup_name,
+				 const char *sfile,
+				 struct objfile *objfile,
+				 struct minimal_symbol **table,
+				 unsigned int hash,
+				 symbol_name_matcher_ftype *matcher,
+				 found_minimal_symbols &found)
+{
+  for (minimal_symbol *msymbol = table[hash];
+       msymbol != NULL;
+       msymbol = msymbol->demangled_hash_next)
+    {
+      const char *symbol_name = MSYMBOL_SEARCH_NAME (msymbol);
+
+      if (matcher (symbol_name, lookup_name, NULL)
+	  && found.maybe_collect (sfile, objfile, msymbol))
+	return;
     }
 }
 
@@ -168,32 +293,22 @@ lookup_minimal_symbol (const char *name, const char *sfile,
 		       struct objfile *objf)
 {
   struct objfile *objfile;
-  struct bound_minimal_symbol found_symbol = { NULL, NULL };
-  struct bound_minimal_symbol found_file_symbol = { NULL, NULL };
-  struct bound_minimal_symbol trampoline_symbol = { NULL, NULL };
+  found_minimal_symbols found;
 
-  unsigned int hash = msymbol_hash (name) % MINIMAL_SYMBOL_HASH_SIZE;
-  unsigned int dem_hash = msymbol_hash_iw (name) % MINIMAL_SYMBOL_HASH_SIZE;
+  unsigned int mangled_hash = msymbol_hash (name) % MINIMAL_SYMBOL_HASH_SIZE;
 
-  const char *modified_name = name;
+  auto *mangled_cmp
+    = (case_sensitivity == case_sensitive_on
+       ? strcmp
+       : strcasecmp);
 
   if (sfile != NULL)
     sfile = lbasename (sfile);
 
-  /* For C++, canonicalize the input name.  */
-  std::string modified_name_storage;
-  if (current_language->la_language == language_cplus)
-    {
-      std::string cname = cp_canonicalize_string (name);
-      if (!cname.empty ())
-	{
-	  std::swap (modified_name_storage, cname);
-	  modified_name = modified_name_storage.c_str ();
-	}
-    }
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
 
   for (objfile = object_files;
-       objfile != NULL && found_symbol.minsym == NULL;
+       objfile != NULL && found.external_symbol.minsym == NULL;
        objfile = objfile->next)
     {
       struct minimal_symbol *msymbol;
@@ -201,131 +316,95 @@ lookup_minimal_symbol (const char *name, const char *sfile,
       if (objf == NULL || objf == objfile
 	  || objf == objfile->separate_debug_objfile_backlink)
 	{
+	  if (symbol_lookup_debug)
+	    {
+	      fprintf_unfiltered (gdb_stdlog,
+				  "lookup_minimal_symbol (%s, %s, %s)\n",
+				  name, sfile != NULL ? sfile : "NULL",
+				  objfile_debug_name (objfile));
+	    }
+
 	  /* Do two passes: the first over the ordinary hash table,
 	     and the second over the demangled hash table.  */
-        int pass;
-
-	if (symbol_lookup_debug)
-	  {
-	    fprintf_unfiltered (gdb_stdlog,
-				"lookup_minimal_symbol (%s, %s, %s)\n",
-				name, sfile != NULL ? sfile : "NULL",
-				objfile_debug_name (objfile));
-	  }
+	  lookup_minimal_symbol_mangled (name, sfile, objfile,
+					 objfile->per_bfd->msymbol_hash,
+					 mangled_hash, mangled_cmp, found);
 
-        for (pass = 1; pass <= 2 && found_symbol.minsym == NULL; pass++)
+	  /* If not found, try the demangled hash table.  */
+	  if (found.external_symbol.minsym == NULL)
 	    {
-            /* Select hash list according to pass.  */
-            if (pass == 1)
-              msymbol = objfile->per_bfd->msymbol_hash[hash];
-            else
-              msymbol = objfile->per_bfd->msymbol_demangled_hash[dem_hash];
-
-            while (msymbol != NULL && found_symbol.minsym == NULL)
+	      /* Once for each language in the demangled hash names
+		 table (usually just zero or one languages).  */
+	      for (auto lang : objfile->per_bfd->demangled_hash_languages)
 		{
-		  int match;
-
-		  if (pass == 1)
-		    {
-		      int (*cmp) (const char *, const char *);
-
-		      cmp = (case_sensitivity == case_sensitive_on
-		             ? strcmp : strcasecmp);
-		      match = cmp (MSYMBOL_LINKAGE_NAME (msymbol),
-				   modified_name) == 0;
-		    }
-		  else
-		    {
-		      /* The function respects CASE_SENSITIVITY.  */
-		      match = MSYMBOL_MATCHES_SEARCH_NAME (msymbol,
-							  modified_name);
-		    }
-
-		  if (match)
-		    {
-                    switch (MSYMBOL_TYPE (msymbol))
-                      {
-                      case mst_file_text:
-                      case mst_file_data:
-                      case mst_file_bss:
-                        if (sfile == NULL
-			    || filename_cmp (msymbol->filename, sfile) == 0)
-			  {
-			    found_file_symbol.minsym = msymbol;
-			    found_file_symbol.objfile = objfile;
-			  }
-                        break;
-
-                      case mst_solib_trampoline:
-
-                        /* If a trampoline symbol is found, we prefer to
-                           keep looking for the *real* symbol.  If the
-                           actual symbol is not found, then we'll use the
-                           trampoline entry.  */
-                        if (trampoline_symbol.minsym == NULL)
-			  {
-			    trampoline_symbol.minsym = msymbol;
-			    trampoline_symbol.objfile = objfile;
-			  }
-                        break;
-
-                      case mst_unknown:
-                      default:
-                        found_symbol.minsym = msymbol;
-			found_symbol.objfile = objfile;
-                        break;
-                      }
-		    }
-
-                /* Find the next symbol on the hash chain.  */
-                if (pass == 1)
-                  msymbol = msymbol->hash_next;
-                else
-                  msymbol = msymbol->demangled_hash_next;
+		  unsigned int hash
+		    = (lookup_name.search_name_hash (lang)
+		       % MINIMAL_SYMBOL_HASH_SIZE);
+
+		  symbol_name_matcher_ftype *match
+		    = language_get_symbol_name_matcher (language_def (lang),
+							lookup_name);
+		  struct minimal_symbol **msymbol_demangled_hash
+		    = objfile->per_bfd->msymbol_demangled_hash;
+
+		  lookup_minimal_symbol_demangled (lookup_name, sfile, objfile,
+						   msymbol_demangled_hash,
+						   hash, match, found);
+
+		  if (found.external_symbol.minsym != NULL)
+		    break;
 		}
 	    }
 	}
     }
 
   /* External symbols are best.  */
-  if (found_symbol.minsym != NULL)
+  if (found.external_symbol.minsym != NULL)
     {
       if (symbol_lookup_debug)
 	{
+	  minimal_symbol *minsym = found.external_symbol.minsym;
+
 	  fprintf_unfiltered (gdb_stdlog,
-			      "lookup_minimal_symbol (...) = %s"
-			      " (external)\n",
-			      host_address_to_string (found_symbol.minsym));
+			      "lookup_minimal_symbol (...) = %s (external)\n",
+			      host_address_to_string (minsym));
 	}
-      return found_symbol;
+      return found.external_symbol;
     }
 
   /* File-local symbols are next best.  */
-  if (found_file_symbol.minsym != NULL)
+  if (found.file_symbol.minsym != NULL)
     {
       if (symbol_lookup_debug)
 	{
+	  minimal_symbol *minsym = found.file_symbol.minsym;
+
 	  fprintf_unfiltered (gdb_stdlog,
-			      "lookup_minimal_symbol (...) = %s"
-			      " (file-local)\n",
-			      host_address_to_string
-			        (found_file_symbol.minsym));
+			      "lookup_minimal_symbol (...) = %s (file-local)\n",
+			      host_address_to_string (minsym));
 	}
-      return found_file_symbol;
+      return found.file_symbol;
     }
 
   /* Symbols for shared library trampolines are next best.  */
-  if (symbol_lookup_debug)
+  if (found.trampoline_symbol.minsym != NULL)
     {
-      fprintf_unfiltered (gdb_stdlog,
-			  "lookup_minimal_symbol (...) = %s%s\n",
-			  trampoline_symbol.minsym != NULL
-			  ? host_address_to_string (trampoline_symbol.minsym)
-			  : "NULL",
-			  trampoline_symbol.minsym != NULL
-			  ? " (trampoline)" : "");
+      if (symbol_lookup_debug)
+	{
+	  minimal_symbol *minsym = found.trampoline_symbol.minsym;
+
+	  fprintf_unfiltered (gdb_stdlog,
+			      "lookup_minimal_symbol (...) = %s (trampoline)\n",
+			      host_address_to_string (minsym));
+	}
+
+      return found.trampoline_symbol;
     }
-  return trampoline_symbol;
+
+  /* Not found.  */
+  if (symbol_lookup_debug)
+    fprintf_unfiltered (gdb_stdlog, "lookup_minimal_symbol (...) = NULL\n");
+  return {};
 }
 
 /* See minsyms.h.  */
@@ -354,34 +433,47 @@ find_minimal_symbol_address (const char *name, CORE_ADDR *addr,
 /* See minsyms.h.  */
 
 void
-iterate_over_minimal_symbols (struct objfile *objf, const char *name,
+iterate_over_minimal_symbols (struct objfile *objf,
+			      const lookup_name_info &lookup_name,
 			      void (*callback) (struct minimal_symbol *,
 						void *),
 			      void *user_data)
 {
-  unsigned int hash;
-  struct minimal_symbol *iter;
-  int (*cmp) (const char *, const char *);
 
   /* The first pass is over the ordinary hash table.  */
-  hash = msymbol_hash (name) % MINIMAL_SYMBOL_HASH_SIZE;
-  iter = objf->per_bfd->msymbol_hash[hash];
-  cmp = (case_sensitivity == case_sensitive_on ? strcmp : strcasecmp);
-  while (iter)
     {
-      if (cmp (MSYMBOL_LINKAGE_NAME (iter), name) == 0)
-	(*callback) (iter, user_data);
-      iter = iter->hash_next;
+      const char *name = lookup_name.name ().c_str ();
+      unsigned int hash = msymbol_hash (name) % MINIMAL_SYMBOL_HASH_SIZE;
+      auto *mangled_cmp
+	= (case_sensitivity == case_sensitive_on
+	   ? strcmp
+	   : strcasecmp);
+
+      for (minimal_symbol *iter = objf->per_bfd->msymbol_hash[hash];
+	   iter != NULL;
+	   iter = iter->hash_next)
+	{
+	  if (mangled_cmp (MSYMBOL_LINKAGE_NAME (iter), name) == 0)
+	    (*callback) (iter, user_data);
+	}
     }
 
-  /* The second pass is over the demangled table.  */
-  hash = msymbol_hash_iw (name) % MINIMAL_SYMBOL_HASH_SIZE;
-  iter = objf->per_bfd->msymbol_demangled_hash[hash];
-  while (iter)
+  /* The second pass is over the demangled table.  Once for each
+     language in the demangled hash names table (usually just zero or
+     one).  */
+  for (auto lang : objf->per_bfd->demangled_hash_languages)
     {
-      if (MSYMBOL_MATCHES_SEARCH_NAME (iter, name))
-	(*callback) (iter, user_data);
-      iter = iter->demangled_hash_next;
+      const language_defn *lang_def = language_def (lang);
+      symbol_name_matcher_ftype *name_match
+	= language_get_symbol_name_matcher (lang_def, lookup_name);
+
+      unsigned int hash
+	= lookup_name.search_name_hash (lang) % MINIMAL_SYMBOL_HASH_SIZE;
+      for (minimal_symbol *iter = objf->per_bfd->msymbol_demangled_hash[hash];
+	   iter != NULL;
+	   iter = iter->demangled_hash_next)
+	if (name_match (MSYMBOL_SEARCH_NAME (iter), lookup_name, NULL))
+	  (*callback) (iter, user_data);
     }
 }
 
@@ -1187,8 +1279,7 @@ build_minimal_symbol_hash_tables (struct objfile *objfile)
 
       msym->demangled_hash_next = 0;
       if (MSYMBOL_SEARCH_NAME (msym) != MSYMBOL_LINKAGE_NAME (msym))
-	add_minsym_to_demangled_hash_table (msym,
-                                            objfile->per_bfd->msymbol_demangled_hash);
+	add_minsym_to_demangled_hash_table (msym, objfile);
     }
 }
 
diff --git a/gdb/minsyms.h b/gdb/minsyms.h
index dc51725..f326be9 100644
--- a/gdb/minsyms.h
+++ b/gdb/minsyms.h
@@ -264,7 +264,7 @@ struct bound_minimal_symbol lookup_minimal_symbol_by_pc (CORE_ADDR);
    USER_DATA as arguments.  */
 
 void iterate_over_minimal_symbols (struct objfile *objf,
-				   const char *name,
+				   const lookup_name_info &name,
 				   void (*callback) (struct minimal_symbol *,
 						     void *),
 				   void *user_data);
diff --git a/gdb/objc-lang.c b/gdb/objc-lang.c
index df2923b..f43f804 100644
--- a/gdb/objc-lang.c
+++ b/gdb/objc-lang.c
@@ -405,7 +405,7 @@ extern const struct language_defn objc_language_defn = {
   default_pass_by_reference,
   default_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/objfiles.h b/gdb/objfiles.h
index 4f11756..12ef5ec 100644
--- a/gdb/objfiles.h
+++ b/gdb/objfiles.h
@@ -27,6 +27,7 @@
 #include "progspace.h"
 #include "registry.h"
 #include "gdb_bfd.h"
+#include <vector>
 
 struct bcache;
 struct htab;
@@ -266,6 +267,12 @@ struct objfile_per_bfd_storage
      demangled names.  */
 
   minimal_symbol *msymbol_demangled_hash[MINIMAL_SYMBOL_HASH_SIZE] {};
+
+  /* All the different languages of symbols found in the demangled
+     hash table.  A flat/vector-based map is more efficient than a map
+     or hash table here, since this will only usually contain zero or
+     one entries.  */
+  std::vector<enum language> demangled_hash_languages;
 };
 
 /* Master structure for keeping track of each file from which
diff --git a/gdb/opencl-lang.c b/gdb/opencl-lang.c
index ffd4c92..1267760 100644
--- a/gdb/opencl-lang.c
+++ b/gdb/opencl-lang.c
@@ -1081,7 +1081,7 @@ extern const struct language_defn opencl_language_defn =
   default_pass_by_reference,
   c_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/p-lang.c b/gdb/p-lang.c
index 2dca923..e93c15b 100644
--- a/gdb/p-lang.c
+++ b/gdb/p-lang.c
@@ -454,7 +454,7 @@ extern const struct language_defn pascal_language_defn =
   default_pass_by_reference,
   default_get_string,
   c_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_compare_symbol_for_completion */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/psymtab.c b/gdb/psymtab.c
index f848990..d7881d2 100644
--- a/gdb/psymtab.c
+++ b/gdb/psymtab.c
@@ -46,7 +46,7 @@ static struct partial_symbol *match_partial_symbol (struct objfile *,
 						    struct partial_symtab *,
 						    int,
 						    const char *, domain_enum,
-						    symbol_compare_ftype *,
+						    symbol_name_match_type,
 						    symbol_compare_ftype *);
 
 static struct partial_symbol *lookup_partial_symbol (struct objfile *,
@@ -510,6 +510,8 @@ psym_lookup_symbol (struct objfile *objfile,
   const int psymtab_index = (block_index == GLOBAL_BLOCK ? 1 : 0);
   struct compunit_symtab *stab_best = NULL;
 
+  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
+
   ALL_OBJFILE_PSYMTABS_REQUIRED (objfile, ps)
   {
     if (!ps->readin && lookup_partial_symbol (objfile, ps, name,
@@ -532,10 +534,10 @@ psym_lookup_symbol (struct objfile *objfile,
 	   information (but NAME might contain it).  */
 
 	if (sym != NULL
-	    && SYMBOL_MATCHES_SEARCH_NAME (sym, name))
+	    && SYMBOL_MATCHES_SEARCH_NAME (sym, lookup_name))
 	  return stab;
 	if (with_opaque != NULL
-	    && SYMBOL_MATCHES_SEARCH_NAME (with_opaque, name))
+	    && SYMBOL_MATCHES_SEARCH_NAME (with_opaque, lookup_name))
 	  stab_best = stab;
 
 	/* Keep looking through other psymtabs.  */
@@ -545,6 +547,18 @@ psym_lookup_symbol (struct objfile *objfile,
   return stab_best;
 }
 
+/* Returns true if PSYM matches LOOKUP_NAME.  */
+
+static bool
+psymbol_name_matches (partial_symbol *psym,
+		      const lookup_name_info &lookup_name)
+{
+  const language_defn *lang = language_def (SYMBOL_LANGUAGE (psym));
+  symbol_name_matcher_ftype *name_match
+    = language_get_symbol_name_matcher (lang, lookup_name);
+  return name_match (SYMBOL_SEARCH_NAME (psym), lookup_name, NULL);
+}
+
 /* Look in PST for a symbol in DOMAIN whose name matches NAME.  Search
    the global block of PST if GLOBAL, and otherwise the static block.
    MATCH is the comparison operation that returns true iff MATCH (s,
@@ -557,7 +571,7 @@ static struct partial_symbol *
 match_partial_symbol (struct objfile *objfile,
 		      struct partial_symtab *pst, int global,
 		      const char *name, domain_enum domain,
-		      symbol_compare_ftype *match,
+		      symbol_name_match_type match_type,
 		      symbol_compare_ftype *ordered_compare)
 {
   struct partial_symbol **start, **psym;
@@ -566,7 +580,10 @@ match_partial_symbol (struct objfile *objfile,
   int do_linear_search = 1;
 
   if (length == 0)
-      return NULL;
+    return NULL;
+
+  lookup_name_info lookup_name (name, match_type);
+
   start = (global ?
 	   &objfile->global_psymbols[pst->globals_offset] :
 	   &objfile->static_psymbols[pst->statics_offset]);
@@ -588,7 +605,12 @@ match_partial_symbol (struct objfile *objfile,
 	{
 	  center = bottom + (top - bottom) / 2;
 	  gdb_assert (center < top);
-	  if (ordered_compare (SYMBOL_SEARCH_NAME (*center), name) >= 0)
+
+	  enum language lang = SYMBOL_LANGUAGE (*center);
+	  const char *lang_ln
+	    = lookup_name.language_lookup_name (lang).c_str ();
+
+	  if (ordered_compare (SYMBOL_SEARCH_NAME (*center), lang_ln) >= 0)
 	    top = center;
 	  else
 	    bottom = center + 1;
@@ -596,7 +618,7 @@ match_partial_symbol (struct objfile *objfile,
       gdb_assert (top == bottom);
 
       while (top <= real_top
-	     && match (SYMBOL_SEARCH_NAME (*top), name) == 0)
+	     && psymbol_name_matches (*top, lookup_name))
 	{
 	  if (symbol_matches_domain (SYMBOL_LANGUAGE (*top),
 				     SYMBOL_DOMAIN (*top), domain))
@@ -614,7 +636,7 @@ match_partial_symbol (struct objfile *objfile,
 	{
 	  if (symbol_matches_domain (SYMBOL_LANGUAGE (*psym),
 				     SYMBOL_DOMAIN (*psym), domain)
-	      && match (SYMBOL_SEARCH_NAME (*psym), name) == 0)
+	      && psymbol_name_matches (*psym, lookup_name))
 	    return *psym;
 	}
     }
@@ -669,6 +691,9 @@ lookup_partial_symbol (struct objfile *objfile,
     return NULL;
 
   gdb::unique_xmalloc_ptr<char> search_name = psymtab_search_name (name);
+
+  lookup_name_info lookup_name (search_name.get (), symbol_name_match_type::FULL);
+
   start = (global ?
 	   &objfile->global_psymbols[pst->globals_offset] :
 	   &objfile->static_psymbols[pst->statics_offset]);
@@ -708,15 +733,13 @@ lookup_partial_symbol (struct objfile *objfile,
 
       /* For `case_sensitivity == case_sensitive_off' strcmp_iw_ordered will
 	 search more exactly than what matches SYMBOL_MATCHES_SEARCH_NAME.  */
-      while (top >= start && SYMBOL_MATCHES_SEARCH_NAME (*top,
-							 search_name.get ()))
+      while (top >= start && SYMBOL_MATCHES_SEARCH_NAME (*top, lookup_name))
 	top--;
 
       /* Fixup to have a symbol which matches SYMBOL_MATCHES_SEARCH_NAME.  */
       top++;
 
-      while (top <= real_top
-	     && SYMBOL_MATCHES_SEARCH_NAME (*top, search_name.get ()))
+      while (top <= real_top && SYMBOL_MATCHES_SEARCH_NAME (*top, lookup_name))
 	{
 	  if (symbol_matches_domain (SYMBOL_LANGUAGE (*top),
 				     SYMBOL_DOMAIN (*top), domain))
@@ -734,7 +757,7 @@ lookup_partial_symbol (struct objfile *objfile,
 	{
 	  if (symbol_matches_domain (SYMBOL_LANGUAGE (*psym),
 				     SYMBOL_DOMAIN (*psym), domain)
-	      && SYMBOL_MATCHES_SEARCH_NAME (*psym, search_name.get ()))
+	      && SYMBOL_MATCHES_SEARCH_NAME (*psym, lookup_name))
 	    return *psym;
 	}
     }
@@ -1213,13 +1236,16 @@ static int
 map_block (const char *name, domain_enum domain, struct objfile *objfile,
 	   struct block *block,
 	   int (*callback) (struct block *, struct symbol *, void *),
-	   void *data, symbol_compare_ftype *match)
+	   void *data, symbol_name_match_type match)
 {
   struct block_iterator iter;
   struct symbol *sym;
 
-  for (sym = block_iter_match_first (block, name, match, &iter);
-       sym != NULL; sym = block_iter_match_next (name, match, &iter))
+  lookup_name_info lookup_name (name, match);
+
+  for (sym = block_iter_match_first (block, lookup_name, &iter);
+       sym != NULL;
+       sym = block_iter_match_next (lookup_name, &iter))
     {
       if (symbol_matches_domain (SYMBOL_LANGUAGE (sym),
 				 SYMBOL_DOMAIN (sym), domain))
@@ -1242,7 +1268,7 @@ psym_map_matching_symbols (struct objfile *objfile,
 			   int (*callback) (struct block *,
 					    struct symbol *, void *),
 			   void *data,
-			   symbol_compare_ftype *match,
+			   symbol_name_match_type match,
 			   symbol_compare_ftype *ordered_compare)
 {
   const int block_kind = global ? GLOBAL_BLOCK : STATIC_BLOCK;
@@ -1277,7 +1303,8 @@ psym_map_matching_symbols (struct objfile *objfile,
 
 static bool
 recursively_search_psymtabs
-  (struct partial_symtab *ps, struct objfile *objfile, enum search_domain kind,
+  (struct partial_symtab *ps, struct objfile *objfile, enum search_domain domain,
+   const lookup_name_info &lookup_name,
    gdb::function_view<expand_symtabs_symbol_matcher_ftype> sym_matcher)
 {
   int keep_going = 1;
@@ -1298,7 +1325,8 @@ recursively_search_psymtabs
 	continue;
 
       r = recursively_search_psymtabs (ps->dependencies[i],
-				       objfile, kind, sym_matcher);
+				       objfile, domain, lookup_name,
+				       sym_matcher);
       if (r != 0)
 	{
 	  ps->searched_flag = PST_SEARCHED_AND_FOUND;
@@ -1332,15 +1360,16 @@ recursively_search_psymtabs
 	{
 	  QUIT;
 
-	  if ((kind == ALL_DOMAIN
-	       || (kind == VARIABLES_DOMAIN
+	  if ((domain == ALL_DOMAIN
+	       || (domain == VARIABLES_DOMAIN
 		   && PSYMBOL_CLASS (*psym) != LOC_TYPEDEF
 		   && PSYMBOL_CLASS (*psym) != LOC_BLOCK)
-	       || (kind == FUNCTIONS_DOMAIN
+	       || (domain == FUNCTIONS_DOMAIN
 		   && PSYMBOL_CLASS (*psym) == LOC_BLOCK)
-	       || (kind == TYPES_DOMAIN
+	       || (domain == TYPES_DOMAIN
 		   && PSYMBOL_CLASS (*psym) == LOC_TYPEDEF))
-	      && sym_matcher (SYMBOL_SEARCH_NAME (*psym)))
+	      && psymbol_name_matches (*psym, lookup_name)
+	      && (sym_matcher == NULL || sym_matcher (SYMBOL_SEARCH_NAME (*psym))))
 	    {
 	      /* Found a match, so notify our caller.  */
 	      result = PST_SEARCHED_AND_FOUND;
@@ -1361,9 +1390,10 @@ static void
 psym_expand_symtabs_matching
   (struct objfile *objfile,
    gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   const lookup_name_info &lookup_name,
    gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
    gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
-   enum search_domain kind)
+   enum search_domain domain)
 {
   struct partial_symtab *ps;
 
@@ -1405,7 +1435,8 @@ psym_expand_symtabs_matching
 	    continue;
 	}
 
-      if (recursively_search_psymtabs (ps, objfile, kind, symbol_matcher))
+      if (recursively_search_psymtabs (ps, objfile, domain,
+				       lookup_name, symbol_matcher))
 	{
 	  struct compunit_symtab *symtab =
 	    psymtab_to_symtab (objfile, ps);
diff --git a/gdb/rust-lang.c b/gdb/rust-lang.c
index 466eb20..c1b2d6e 100644
--- a/gdb/rust-lang.c
+++ b/gdb/rust-lang.c
@@ -2250,7 +2250,7 @@ extern const struct language_defn rust_language_defn =
   default_pass_by_reference,
   c_get_string,
   rust_watch_location_expression,
-  NULL,				/* la_get_symbol_name_cmp */
+  NULL,				/* la_get_symbol_name_matcher */
   iterate_over_symbols,
   default_search_name_hash,
   &default_varobj_ops,
diff --git a/gdb/symfile-debug.c b/gdb/symfile-debug.c
index d67985f..49c472e0 100644
--- a/gdb/symfile-debug.c
+++ b/gdb/symfile-debug.c
@@ -260,7 +260,7 @@ debug_qf_map_matching_symbols (struct objfile *objfile,
 			       int (*callback) (struct block *,
 						struct symbol *, void *),
 			       void *data,
-			       symbol_compare_ftype *match,
+			       symbol_name_match_type match,
 			       symbol_compare_ftype *ordered_compare)
 {
   const struct debug_sym_fns_data *debug_data
@@ -273,7 +273,7 @@ debug_qf_map_matching_symbols (struct objfile *objfile,
 		    domain_name (domain), global,
 		    host_address_to_string (callback),
 		    host_address_to_string (data),
-		    host_address_to_string (match),
+		    plongest ((LONGEST) match),
 		    host_address_to_string (ordered_compare));
 
   debug_data->real_sf->qf->map_matching_symbols (objfile, name,
@@ -287,6 +287,7 @@ static void
 debug_qf_expand_symtabs_matching
   (struct objfile *objfile,
    gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   const lookup_name_info &lookup_name,
    gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
    gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
    enum search_domain kind)
@@ -305,6 +306,7 @@ debug_qf_expand_symtabs_matching
 
   debug_data->real_sf->qf->expand_symtabs_matching (objfile,
 						    file_matcher,
+						    lookup_name,
 						    symbol_matcher,
 						    expansion_notify,
 						    kind);
diff --git a/gdb/symfile.c b/gdb/symfile.c
index 71fa09b..feb50f8 100644
--- a/gdb/symfile.c
+++ b/gdb/symfile.c
@@ -3773,6 +3773,7 @@ symfile_free_objfile (struct objfile *objfile)
 void
 expand_symtabs_matching
   (gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   const lookup_name_info &lookup_name,
    gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
    gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
    enum search_domain kind)
@@ -3783,6 +3784,7 @@ expand_symtabs_matching
   {
     if (objfile->sf)
       objfile->sf->qf->expand_symtabs_matching (objfile, file_matcher,
+						lookup_name,
 						symbol_matcher,
 						expansion_notify, kind);
   }
diff --git a/gdb/symfile.h b/gdb/symfile.h
index 14f48f3..3472aa0 100644
--- a/gdb/symfile.h
+++ b/gdb/symfile.h
@@ -231,7 +231,7 @@ struct quick_symbol_functions
 				int (*callback) (struct block *,
 						 struct symbol *, void *),
 				void *data,
-				symbol_compare_ftype *match,
+				symbol_name_match_type match,
 				symbol_compare_ftype *ordered_compare);
 
   /* Expand all symbol tables in OBJFILE matching some criteria.
@@ -255,6 +255,7 @@ struct quick_symbol_functions
   void (*expand_symtabs_matching)
     (struct objfile *objfile,
      gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+     const lookup_name_info &lookup_name,
      gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
      gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
      enum search_domain kind);
@@ -526,6 +527,7 @@ extern scoped_restore_tmpl<int> increment_reading_symtab (void);
 
 void expand_symtabs_matching
   (gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   const lookup_name_info &lookup_name,
    gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
    gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
    enum search_domain kind);
diff --git a/gdb/symmisc.c b/gdb/symmisc.c
index ed2e8d2..96aa30f 100644
--- a/gdb/symmisc.c
+++ b/gdb/symmisc.c
@@ -949,6 +949,7 @@ maintenance_expand_symtabs (const char *args, int from_tty)
 	       return (!basenames
 		       && (regexp == NULL || re_exec (filename)));
 	     },
+	     lookup_name_info::match_any (),
 	     [] (const char *symname)
 	     {
 	       /* Since we're not searching on symbols, just return true.  */
diff --git a/gdb/symtab.c b/gdb/symtab.c
index 1e58770..aecee8f 100644
--- a/gdb/symtab.c
+++ b/gdb/symtab.c
@@ -946,6 +946,19 @@ symbol_search_name (const struct general_symbol_info *gsymbol)
   else
     return symbol_natural_name (gsymbol);
 }
+
+/* See symtab.h.  */
+
+bool
+symbol_matches_search_name (const struct general_symbol_info *gsymbol,
+			    const lookup_name_info &name)
+{
+  symbol_name_matcher_ftype *name_match
+    = language_get_symbol_name_matcher (language_def (gsymbol->language),
+					name);
+  return name_match (symbol_search_name (gsymbol), name, NULL);
+}
+
 
 
 /* Return 1 if the two sections are the same, or if they could
@@ -1103,11 +1116,12 @@ eq_symbol_entry (const struct symbol_cache_slot *slot,
     }
   else if (slot_name != NULL && name != NULL)
     {
-      /* It's important that we use the same comparison that was done the
-	 first time through.  If the slot records a found symbol, then this
-	 means using strcmp_iw on SYMBOL_SEARCH_NAME.  See dictionary.c.
-	 It also means using symbol_matches_domain for found symbols.
-	 See block.c.
+      /* It's important that we use the same comparison that was done
+	 the first time through.  If the slot records a found symbol,
+	 then this means using the symbol name comparison function of
+	 the symbol's language with SYMBOL_SEARCH_NAME.  See
+	 dictionary.c.  It also means using symbol_matches_domain for
+	 found symbols.  See block.c.
 
 	 If the slot records a not-found symbol, then require a precise match.
 	 We could still be lax with whitespace like strcmp_iw though.  */
@@ -1122,9 +1136,11 @@ eq_symbol_entry (const struct symbol_cache_slot *slot,
       else
 	{
 	  struct symbol *sym = slot->value.found.symbol;
+	  lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
 
-	  if (strcmp_iw (slot_name, name) != 0)
+	  if (!SYMBOL_MATCHES_SEARCH_NAME (sym, lookup_name))
 	    return 0;
+
 	  if (!symbol_matches_domain (SYMBOL_LANGUAGE (sym),
 				      slot_domain, domain))
 	    return 0;
@@ -1743,6 +1759,30 @@ fixup_symbol_section (struct symbol *sym, struct objfile *objfile)
   return sym;
 }
 
+/* See symtab.h.  */
+
+demangle_for_lookup_info::demangle_for_lookup_info
+  (const lookup_name_info &lookup_name, language lang)
+{
+  demangle_result_storage storage;
+
+  m_demangled_name = demangle_for_lookup (lookup_name.name ().c_str (),
+					  lang, storage);
+}
+
+/* See symtab.h.  */
+
+const lookup_name_info &
+lookup_name_info::match_any ()
+{
+  /* Lookup any symbol that "" would complete.  I.e., this matches all
+     symbol names.  */
+  static const lookup_name_info lookup_name ({}, symbol_name_match_type::FULL,
+					     true);
+
+  return lookup_name;
+}
+
 /* Compute the demangled form of NAME as used by the various symbol
    lookup functions.  The result can either be the input NAME
    directly, or a pointer to a buffer owned by the STORAGE object.
@@ -2767,7 +2807,8 @@ basic_lookup_transparent_type (const char *name)
    search continues.  */
 
 void
-iterate_over_symbols (const struct block *block, const char *name,
+iterate_over_symbols (const struct block *block,
+		      const lookup_name_info &name,
 		      const domain_enum domain,
 		      gdb::function_view<symbol_found_callback_ftype> callback)
 {
@@ -4231,6 +4272,7 @@ search_symbols (const char *regexp, enum search_domain kind,
 			     return file_matches (filename, files, nfiles,
 						  basenames);
 			   },
+			   lookup_name_info::match_any (),
 			   [&] (const char *symname)
 			   {
 			     return (!preg || preg->exec (symname,
@@ -4587,13 +4629,33 @@ rbreak_command (const char *regexp, int from_tty)
    information.  */
 
 static int
-compare_symbol_name (const char *name, const char *sym_text, int sym_text_len)
-{
-  int (*ncmp) (const char *, const char *, size_t);
+compare_symbol_name (const char *name,
+		     language symbol_language,
+		     const lookup_name_info &lookup_name,
+		     const char *sym_text, int sym_text_len,
+		     completion_match_result &match_res)
+{
+  const language_defn *lang;
+
+  /* If we're completing for an expression and the symbol doesn't have
+     an explicit language set, fallback to the current language.  Ada
+     minimal symbols won't have their language set to Ada, for
+     example, and if we compared using the default/C-like matcher,
+     then when completing e.g., symbols in a package named "pck", we'd
+     match internal Ada symbols like "pckS", which are invalid in an
+     Ada expression, unless you wrap them in '<' '>' to request a
+     verbatim match.  */
+  if (symbol_language == language_auto
+      && lookup_name.match_type () == symbol_name_match_type::EXPRESSION)
+    lang = current_language;
+  else
+    lang = language_def (symbol_language);
 
-  ncmp = (case_sensitivity == case_sensitive_on ? strncmp : strncasecmp);
+  symbol_name_matcher_ftype *name_match
+    = language_get_symbol_name_matcher (lang, lookup_name);
 
-  if (ncmp (name, sym_text, sym_text_len) != 0)
+  /* Clip symbols that cannot match.  */
+  if (!name_match (name, lookup_name, &match_res.match))
     return 0;
 
   if (sym_text[sym_text_len] == '(')
@@ -4611,20 +4673,32 @@ compare_symbol_name (const char *name, const char *sym_text, int sym_text_len)
   return 1;
 }
 
-/*  Test to see if the symbol specified by SYMNAME (which is already
-   demangled for C++ symbols) matches SYM_TEXT in the first SYM_TEXT_LEN
-   characters.  If so, add it to the current completion list.  */
+/*  See symtab.h.  */
 
-static void
+void
 completion_list_add_name (completion_tracker &tracker,
+			  language symbol_language,
 			  const char *symname,
+			  const lookup_name_info &lookup_name,
 			  const char *sym_text, int sym_text_len,
 			  const char *text, const char *word)
 {
+  completion_match_result &match_res
+    = tracker.reset_completion_match_result ();
+
   /* Clip symbols that cannot match.  */
-  if (!compare_symbol_name (symname, sym_text, sym_text_len))
+  if (!compare_symbol_name (symname, symbol_language,
+			    lookup_name,
+			    sym_text, sym_text_len,
+			    match_res))
     return;
 
+  /* Refresh SYMNAME from the match string.  It's potentially
+     different depending on language.  (E.g., on Ada, the match may be
+     the encoded symbol name wrapped in "<>").  */
+  symname = match_res.match.match ();
+  gdb_assert (symname != NULL);
+
   /* We have a match for a completion, so add SYMNAME to the current list
      of matches.  Note that the name is moved to freshly malloc'd space.  */
 
@@ -4662,11 +4736,13 @@ completion_list_add_name (completion_tracker &tracker,
 static void
 completion_list_add_symbol (completion_tracker &tracker,
 			    symbol *sym,
+			    const lookup_name_info &lookup_name,
 			    const char *sym_text, int sym_text_len,
 			    const char *text, const char *word)
 {
-  completion_list_add_name (tracker, SYMBOL_NATURAL_NAME (sym),
-			    sym_text, sym_text_len, text, word);
+  completion_list_add_name (tracker, SYMBOL_LANGUAGE (sym),
+			    SYMBOL_NATURAL_NAME (sym),
+			    lookup_name, sym_text, sym_text_len, text, word);
 }
 
 /* completion_list_add_name wrapper for struct minimal_symbol.  */
@@ -4674,19 +4750,23 @@ completion_list_add_symbol (completion_tracker &tracker,
 static void
 completion_list_add_msymbol (completion_tracker &tracker,
 			     minimal_symbol *sym,
+			     const lookup_name_info &lookup_name,
 			     const char *sym_text, int sym_text_len,
 			     const char *text, const char *word)
 {
-  completion_list_add_name (tracker, MSYMBOL_NATURAL_NAME (sym),
-			    sym_text, sym_text_len, text, word);
+  completion_list_add_name (tracker, MSYMBOL_LANGUAGE (sym),
+			    MSYMBOL_NATURAL_NAME (sym),
+			    lookup_name, sym_text, sym_text_len, text, word);
 }
 
+
 /* ObjC: In case we are completing on a selector, look as the msymbol
    again and feed all the selectors into the mill.  */
 
 static void
 completion_list_objc_symbol (completion_tracker &tracker,
 			     struct minimal_symbol *msymbol,
+			     const lookup_name_info &lookup_name,
 			     const char *sym_text, int sym_text_len,
 			     const char *text, const char *word)
 {
@@ -4704,7 +4784,9 @@ completion_list_objc_symbol (completion_tracker &tracker,
 
   if (sym_text[0] == '[')
     /* Complete on shortened method method.  */
-    completion_list_add_name (tracker, method + 1,
+    completion_list_add_name (tracker, language_objc,
+			      method + 1,
+			      lookup_name,
 			      sym_text, sym_text_len, text, word);
 
   while ((strlen (method) + 1) >= tmplen)
@@ -4726,10 +4808,12 @@ completion_list_objc_symbol (completion_tracker &tracker,
       memcpy (tmp, method, (category - method));
       tmp[category - method] = ' ';
       memcpy (tmp + (category - method) + 1, selector, strlen (selector) + 1);
-      completion_list_add_name (tracker, tmp,
+      completion_list_add_name (tracker, language_objc, tmp,
+				lookup_name,
 				sym_text, sym_text_len, text, word);
       if (sym_text[0] == '[')
-	completion_list_add_name (tracker, tmp + 1,
+	completion_list_add_name (tracker, language_objc, tmp + 1,
+				  lookup_name,
 				  sym_text, sym_text_len, text, word);
     }
 
@@ -4741,7 +4825,8 @@ completion_list_objc_symbol (completion_tracker &tracker,
       if (tmp2 != NULL)
 	*tmp2 = '\0';
 
-      completion_list_add_name (tracker, tmp,
+      completion_list_add_name (tracker, language_objc, tmp,
+				lookup_name,
 				sym_text, sym_text_len, text, word);
     }
 }
@@ -4795,6 +4880,7 @@ language_search_unquoted_string (const char *text, const char *p)
 static void
 completion_list_add_fields (completion_tracker &tracker,
 			    struct symbol *sym,
+			    const lookup_name_info &lookup_name,
 			    const char *sym_text, int sym_text_len,
 			    const char *text, const char *word)
 {
@@ -4807,7 +4893,9 @@ completion_list_add_fields (completion_tracker &tracker,
       if (c == TYPE_CODE_UNION || c == TYPE_CODE_STRUCT)
 	for (j = TYPE_N_BASECLASSES (t); j < TYPE_NFIELDS (t); j++)
 	  if (TYPE_FIELD_NAME (t, j))
-	    completion_list_add_name (tracker, TYPE_FIELD_NAME (t, j),
+	    completion_list_add_name (tracker, SYMBOL_LANGUAGE (sym),
+				      TYPE_FIELD_NAME (t, j),
+				      lookup_name,
 				      sym_text, sym_text_len, text, word);
     }
 }
@@ -4817,6 +4905,7 @@ completion_list_add_fields (completion_tracker &tracker,
 static void
 add_symtab_completions (struct compunit_symtab *cust,
 			completion_tracker &tracker,
+			const lookup_name_info &lookup_name,
 			const char *sym_text, int sym_text_len,
 			const char *text, const char *word,
 			enum type_code code)
@@ -4839,6 +4928,7 @@ add_symtab_completions (struct compunit_symtab *cust,
 	      || (SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN
 		  && TYPE_CODE (SYMBOL_TYPE (sym)) == code))
 	    completion_list_add_symbol (tracker, sym,
+					lookup_name,
 					sym_text, sym_text_len,
 					text, word);
 	}
@@ -4847,8 +4937,8 @@ add_symtab_completions (struct compunit_symtab *cust,
 
 void
 default_collect_symbol_completion_matches_break_on
-  (completion_tracker &tracker,
-   complete_symbol_mode mode,
+  (completion_tracker &tracker, complete_symbol_mode mode,
+   symbol_name_match_type name_match_type,
    const char *text, const char *word,
    const char *break_on, enum type_code code)
 {
@@ -4939,6 +5029,9 @@ default_collect_symbol_completion_matches_break_on
     }
   gdb_assert (sym_text[sym_text_len] == '\0' || sym_text[sym_text_len] == '(');
 
+  lookup_name_info lookup_name (std::string (sym_text, sym_text_len),
+				name_match_type, true);
+
   /* At this point scan through the misc symbol vectors and add each
      symbol you find to the list.  Eventually we want to ignore
      anything that isn't a text symbol (everything else will be
@@ -4950,34 +5043,30 @@ default_collect_symbol_completion_matches_break_on
 	{
 	  QUIT;
 
-	  completion_list_add_msymbol (tracker,
-				       msymbol, sym_text, sym_text_len,
+	  completion_list_add_msymbol (tracker, msymbol, lookup_name,
+				       sym_text, sym_text_len,
 				       text, word);
 
-	  completion_list_objc_symbol (tracker,
-				       msymbol, sym_text, sym_text_len,
-				       text, word);
+	  completion_list_objc_symbol (tracker, msymbol, lookup_name,
+				       sym_text, sym_text_len, text,
+				       word);
 	}
     }
 
   /* Add completions for all currently loaded symbol tables.  */
   ALL_COMPUNITS (objfile, cust)
-    add_symtab_completions (cust, tracker,
+    add_symtab_completions (cust, tracker, lookup_name,
 			    sym_text, sym_text_len, text, word, code);
 
   /* Look through the partial symtabs for all symbols which begin by
      matching SYM_TEXT.  Expand all CUs that you find to the list.  */
   expand_symtabs_matching (NULL,
-			   [&] (const char *name) /* symbol matcher */
-			     {
-			       return compare_symbol_name (name,
-							   sym_text,
-							   sym_text_len);
-			     },
+			   lookup_name,
+			   NULL,
 			   [&] (compunit_symtab *symtab) /* expansion notify */
 			     {
 			       add_symtab_completions (symtab,
-						       tracker,
+						       tracker, lookup_name,
 						       sym_text, sym_text_len,
 						       text, word, code);
 			     },
@@ -5000,16 +5089,16 @@ default_collect_symbol_completion_matches_break_on
 	  {
 	    if (code == TYPE_CODE_UNDEF)
 	      {
-		completion_list_add_symbol (tracker, sym,
+		completion_list_add_symbol (tracker, sym, lookup_name,
 					    sym_text, sym_text_len, text,
 					    word);
-		completion_list_add_fields (tracker, sym,
+		completion_list_add_fields (tracker, sym, lookup_name,
 					    sym_text, sym_text_len, text,
 					    word);
 	      }
 	    else if (SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN
 		     && TYPE_CODE (SYMBOL_TYPE (sym)) == code)
-	      completion_list_add_symbol (tracker, sym,
+	      completion_list_add_symbol (tracker, sym, lookup_name,
 					  sym_text, sym_text_len, text,
 					  word);
 	  }
@@ -5028,12 +5117,12 @@ default_collect_symbol_completion_matches_break_on
     {
       if (surrounding_static_block != NULL)
 	ALL_BLOCK_SYMBOLS (surrounding_static_block, iter, sym)
-	  completion_list_add_fields (tracker, sym,
+	  completion_list_add_fields (tracker, sym, lookup_name,
 				      sym_text, sym_text_len, text, word);
 
       if (surrounding_global_block != NULL)
 	ALL_BLOCK_SYMBOLS (surrounding_global_block, iter, sym)
-	  completion_list_add_fields (tracker, sym,
+	  completion_list_add_fields (tracker, sym, lookup_name,
 				      sym_text, sym_text_len, text, word);
     }
 
@@ -5050,7 +5139,10 @@ default_collect_symbol_completion_matches_break_on
 				 macro_source_file *,
 				 int)
 	{
-	  completion_list_add_name (tracker, macro_name,
+	  completion_list_add_name (tracker,
+				    language_c,
+				    macro_name,
+				    lookup_name,
 				    sym_text, sym_text_len,
 				    text, word);
 	};
@@ -5078,10 +5170,12 @@ default_collect_symbol_completion_matches_break_on
 void
 default_collect_symbol_completion_matches (completion_tracker &tracker,
 					   complete_symbol_mode mode,
+					   symbol_name_match_type name_match_type,
 					   const char *text, const char *word,
 					   enum type_code code)
 {
   return default_collect_symbol_completion_matches_break_on (tracker, mode,
+							     name_match_type,
 							     text, word, "",
 							     code);
 }
@@ -5092,9 +5186,11 @@ default_collect_symbol_completion_matches (completion_tracker &tracker,
 void
 collect_symbol_completion_matches (completion_tracker &tracker,
 				   complete_symbol_mode mode,
+				   symbol_name_match_type name_match_type,
 				   const char *text, const char *word)
 {
   current_language->la_collect_symbol_completion_matches (tracker, mode,
+							  name_match_type,
 							  text, word,
 							  TYPE_CODE_UNDEF);
 }
@@ -5108,11 +5204,13 @@ collect_symbol_completion_matches_type (completion_tracker &tracker,
 					enum type_code code)
 {
   complete_symbol_mode mode = complete_symbol_mode::EXPRESSION;
+  symbol_name_match_type name_match_type = symbol_name_match_type::EXPRESSION;
 
   gdb_assert (code == TYPE_CODE_UNION
 	      || code == TYPE_CODE_STRUCT
 	      || code == TYPE_CODE_ENUM);
   current_language->la_collect_symbol_completion_matches (tracker, mode,
+							  name_match_type,
 							  text, word, code);
 }
 
@@ -5122,6 +5220,7 @@ collect_symbol_completion_matches_type (completion_tracker &tracker,
 void
 collect_file_symbol_completion_matches (completion_tracker &tracker,
 					complete_symbol_mode mode,
+					symbol_name_match_type name_match_type,
 					const char *text, const char *word,
 					const char *srcfile)
 {
@@ -5178,12 +5277,15 @@ collect_file_symbol_completion_matches (completion_tracker &tracker,
 
   sym_text_len = strlen (sym_text);
 
+  lookup_name_info lookup_name (std::string (sym_text, sym_text_len),
+				name_match_type, true);
+
   /* Go through symtabs for SRCFILE and check the externs and statics
      for symbols which match.  */
   iterate_over_symtabs (srcfile, [&] (symtab *s)
     {
       add_symtab_completions (SYMTAB_COMPUNIT (s),
-			      tracker,
+			      tracker, lookup_name,
 			      sym_text, sym_text_len,
 			      text, word, TYPE_CODE_UNDEF);
       return false;
diff --git a/gdb/symtab.h b/gdb/symtab.h
index d5c929d..5dfe953 100644
--- a/gdb/symtab.h
+++ b/gdb/symtab.h
@@ -21,10 +21,12 @@
 #define SYMTAB_H 1
 
 #include <vector>
+#include <string>
 #include "gdb_vecs.h"
 #include "gdbtypes.h"
 #include "common/enum-flags.h"
 #include "common/function-view.h"
+#include "common/gdb_optional.h"
 #include "completer.h"
 
 /* Opaque declarations.  */
@@ -43,6 +45,251 @@ struct probe;
 struct common_block;
 struct obj_section;
 struct cmd_list_element;
+struct lookup_name_info;
+
+/* How to match a lookup name against a symbol search name.  */
+enum class symbol_name_match_type
+{
+  /* Wild matching.  Matches unqualified symbol names in all
+     namespace/module/packages, etc.  */
+  WILD,
+
+  /* Full matching.  The lookup name indicates a fully-qualified name,
+     and only matches symbol search names in the specified
+     namespace/module/package.  */
+  FULL,
+
+  /* Expression matching.  The same as FULL matching in most
+     languages.  The same as WILD matching in Ada.  */
+  EXPRESSION,
+};
+
+/* Hash the given symbol search name according to LANGUAGE's
+   rules.  */
+extern unsigned int search_name_hash (enum language language,
+				      const char *search_name);
+
+/* Ada-specific bits of a lookup_name_info object.  This is lazily
+   constructed on demand.  */
+
+class ada_lookup_name_info final
+{
+ public:
+  /* Construct.  */
+  explicit ada_lookup_name_info (const lookup_name_info &lookup_name);
+
+  /* Compare SYMBOL_SEARCH_NAME with our lookup name, using MATCH_TYPE
+     as name match type.  Returns true if there's a match, false
+     otherwise.  If non-NULL, store the matching results in MATCH.  */
+  bool matches (const char *symbol_search_name,
+		symbol_name_match_type match_type,
+		completion_match *match) const;
+
+  /* The Ada-encoded lookup name.  */
+  const std::string &lookup_name () const
+  { return m_encoded_name; }
+
+  /* Return true if we're supposed to be doing a wild match look
+     up.  */
+  bool wild_match_p () const
+  { return m_wild_match_p; }
+
+  /* Return true if we're looking up a name inside package
+     Standard.  */
+  bool standard_p () const
+  { return m_standard_p; }
+
+ private:
+  /* The Ada-encoded lookup name.  */
+  std::string m_encoded_name;
+
+  /* Whether the user-provided lookup name was Ada encoded.  If so,
+     then return encoded names in the 'matches' method's 'completion
+     match result' output.  */
+  bool m_encoded_p : 1;
+
+  /* True if really doing wild matching.  Even if the user requests
+     wild matching, some cases require full matching.  */
+  bool m_wild_match_p : 1;
+
+  /* True if doing a verbatim match.  This is true if the decoded
+     version of the symbol name is wrapped in '<'/'>'.  This is an
+     escape hatch users can use to look up symbols the Ada encoding
+     does not understand.  */
+  bool m_verbatim_p : 1;
+
+   /* True if the user specified a symbol name that is inside package
+      Standard.  Symbol names inside package Standard are handled
+      specially.  We always do a non-wild match of the symbol name
+      without the "standard__" prefix, and only search static and
+      global symbols.  This was primarily introduced in order to allow
+      the user to specifically access the standard exceptions using,
+      for instance, Standard.Constraint_Error when Constraint_Error is
+      ambiguous (due to the user defining its own Constraint_Error
+      entity inside its program).  */
+  bool m_standard_p : 1;
+};
+
+/* Language-specific bits of a lookup_name_info object, for languages
+   that do name searching using demangled names (C++/D/Go).  This is
+   lazily constructed on demand.  */
+
+struct demangle_for_lookup_info final
+{
+public:
+  demangle_for_lookup_info (const lookup_name_info &lookup_name,
+			    language lang);
+
+  /* The demangled lookup name.  */
+  const std::string &lookup_name () const
+  { return m_demangled_name; }
+
+private:
+  /* The demangled lookup name.  */
+  std::string m_demangled_name;
+};
+
+/* Object that aggregates all information related to a symbol lookup
+   name.  I.e., the name that is matched against the symbol's search
+   name.  Caches per-language information so that it doesn't require
+   recomputing it for every symbol comparison, like for example the
+   Ada encoded name and the symbol's name hash for a given language.
+   The object is conceptually immutable once constructed, and thus has
+   no setters.  This is to prevent some code path from tweaking some
+   property of the lookup name for some local reason and accidentally
+   altering the results of any continuing search(es).
+   lookup_name_info objects are generally passed around as a const
+   reference to reinforce that.  (They're not passed around by value
+   because they're not small.)  */
+class lookup_name_info final
+{
+ public:
+  /* Create a new object.  */
+  lookup_name_info (std::string name,
+		    symbol_name_match_type match_type,
+		    bool completion_mode = false)
+    : m_match_type (match_type),
+      m_completion_mode (completion_mode),
+      m_name (std::move (name))
+  {}
+
+  /* Getters.  See description of each corresponding field.  */
+  symbol_name_match_type match_type () const { return m_match_type; }
+  bool completion_mode () const { return m_completion_mode; }
+  const std::string &name () const { return m_name; }
+
+  /* Get the search name hash for searches in language LANG.  */
+  unsigned int search_name_hash (language lang) const
+  {
+    /* Only compute each language's hash once.  */
+    if (!m_demangled_hashes_p[lang])
+      {
+	m_demangled_hashes[lang]
+	  = ::search_name_hash (lang, language_lookup_name (lang).c_str ());
+	m_demangled_hashes_p[lang] = true;
+      }
+    return m_demangled_hashes[lang];
+  }
+
+  /* Get the search name for searches in language LANG.  */
+  const std::string &language_lookup_name (language lang) const
+  {
+    switch (lang)
+      {
+      case language_ada:
+	return ada ().lookup_name ();
+      case language_cplus:
+	return cplus ().lookup_name ();
+      case language_d:
+	return d ().lookup_name ();
+      case language_go:
+	return go ().lookup_name ();
+      default:
+	return m_name;
+      }
+  }
+
+  /* Get the Ada-specific lookup info.  */
+  const ada_lookup_name_info &ada () const
+  {
+    maybe_init (m_ada);
+    return *m_ada;
+  }
+
+  /* Get the C++-specific lookup info.  */
+  const demangle_for_lookup_info &cplus () const
+  {
+    maybe_init (m_cplus, language_cplus);
+    return *m_cplus;
+  }
+
+  /* Get the D-specific lookup info.  */
+  const demangle_for_lookup_info &d () const
+  {
+    maybe_init (m_d, language_d);
+    return *m_d;
+  }
+
+  /* Get the Go-specific lookup info.  */
+  const demangle_for_lookup_info &go () const
+  {
+    maybe_init (m_go, language_go);
+    return *m_go;
+  }
+
+  /* Get a reference to a lookup_name_info object that matches any
+     symbol name.  */
+  static const lookup_name_info &match_any ();
+
+private:
+  /* Initialize FIELD, if not initialized yet.  */
+  template<typename Field, typename... Args>
+  void maybe_init (Field &field, Args&&... args) const
+  {
+    if (!field)
+      field.emplace (*this, std::forward<Args> (args)...);
+  }
+
+  /* The lookup info as passed to the ctor.  */
+  symbol_name_match_type m_match_type;
+  bool m_completion_mode;
+  std::string m_name;
+
+  /* Language-specific info.  These fields are filled lazily the first
+     time a lookup is done in the corresponding language.  They're
+     mutable because lookup_name_info objects are typically passed
+     around by const reference (see intro), and they're conceptually
+     "cache" that can always be reconstructed from the non-mutable
+     fields.  */
+  mutable gdb::optional<ada_lookup_name_info> m_ada;
+  mutable gdb::optional<demangle_for_lookup_info> m_cplus;
+  mutable gdb::optional<demangle_for_lookup_info> m_d;
+  mutable gdb::optional<demangle_for_lookup_info> m_go;
+
+  /* The demangled hashes.  Stored in an array with one entry for each
+     possible language.  The second array records whether we've
+     already computed the each language's hash.  (These are separate
+     arrays instead of a single array of optional<unsigned> to avoid
+     alignment padding).  */
+  mutable std::array<unsigned int, nr_languages> m_demangled_hashes;
+  mutable std::array<bool, nr_languages> m_demangled_hashes_p {};
+};
+
+/* Comparison function for completion symbol lookup.
+
+   Returns true if the symbol name matches against LOOKUP_NAME.
+
+   SYMBOL_SEARCH_NAME should be a symbol's "search" name.
+
+   On success and if non-NULL, MATCH is set to point to the symbol
+   name as should be presented to the user as a completion match list
+   element.  In most languages, this is the same as the symbol's
+   search name, but in some, like Ada, the display name is dynamically
+   computed within the comparison routine.  */
+typedef bool (symbol_name_matcher_ftype)
+  (const char *symbol_search_name,
+   const lookup_name_info &lookup_name,
+   completion_match *match);
 
 /* Some of the structures in this file are space critical.
    The space-critical structures are:
@@ -269,13 +516,18 @@ extern int demangle;
    returns the same value (same pointer) as SYMBOL_LINKAGE_NAME.  */
 #define SYMBOL_SEARCH_NAME(symbol)					 \
    (symbol_search_name (&(symbol)->ginfo))
-extern const char *symbol_search_name (const struct general_symbol_info *);
+extern const char *symbol_search_name (const struct general_symbol_info *ginfo);
 
-/* Return non-zero if NAME matches the "search" name of SYMBOL.
-   Whitespace and trailing parentheses are ignored.
-   See strcmp_iw for details about its behavior.  */
-#define SYMBOL_MATCHES_SEARCH_NAME(symbol, name)			\
-  (strcmp_iw (SYMBOL_SEARCH_NAME (symbol), (name)) == 0)
+/* Return true if NAME matches the "search" name of SYMBOL, according
+   to the symbol's language.  */
+#define SYMBOL_MATCHES_SEARCH_NAME(symbol, name)                       \
+  symbol_matches_search_name (&(symbol)->ginfo, (name))
+
+/* Helper for SYMBOL_MATCHES_SEARCH_NAME that works with both symbols
+   and psymbols.  */
+extern bool symbol_matches_search_name
+  (const struct general_symbol_info *gsymbol,
+   const lookup_name_info &name);
 
 /* Compute the hash of the given symbol search name of a symbol of
    language LANGUAGE.  */
@@ -427,8 +679,6 @@ struct minimal_symbol
   (symbol_set_language (&(symbol)->mginfo, (language), (obstack)))
 #define MSYMBOL_SEARCH_NAME(symbol)					 \
    (symbol_search_name (&(symbol)->mginfo))
-#define MSYMBOL_MATCHES_SEARCH_NAME(symbol, name)			\
-  (strcmp_iw (MSYMBOL_SEARCH_NAME (symbol), (name)) == 0)
 #define MSYMBOL_SET_NAMES(symbol,linkage_name,len,copy_name,objfile)	\
   symbol_set_names (&(symbol)->mginfo, linkage_name, len, copy_name, objfile)
 
@@ -1512,26 +1762,30 @@ enum class complete_symbol_mode
 extern void default_collect_symbol_completion_matches_break_on
   (completion_tracker &tracker,
    complete_symbol_mode mode,
+   symbol_name_match_type name_match_type,
    const char *text, const char *word, const char *break_on,
    enum type_code code);
 extern void default_collect_symbol_completion_matches
   (completion_tracker &tracker,
    complete_symbol_mode,
+   symbol_name_match_type name_match_type,
    const char *,
    const char *,
    enum type_code);
-extern void collect_symbol_completion_matches (completion_tracker &tracker,
-					       complete_symbol_mode,
-					       const char *, const char *);
+extern void collect_symbol_completion_matches
+  (completion_tracker &tracker,
+   complete_symbol_mode mode,
+   symbol_name_match_type name_match_type,
+   const char *, const char *);
 extern void collect_symbol_completion_matches_type (completion_tracker &tracker,
 						    const char *, const char *,
 						    enum type_code);
 
-extern void collect_file_symbol_completion_matches (completion_tracker &tracker,
-						    complete_symbol_mode,
-						    const char *,
-						    const char *,
-						    const char *);
+extern void collect_file_symbol_completion_matches
+  (completion_tracker &tracker,
+   complete_symbol_mode,
+   symbol_name_match_type name_match_type,
+   const char *, const char *, const char *);
 
 extern completion_list
   make_source_files_completion_list (const char *, const char *);
@@ -1680,7 +1934,8 @@ std::vector<CORE_ADDR> find_pcs_for_symtab_line
 
 typedef bool (symbol_found_callback_ftype) (symbol *sym);
 
-void iterate_over_symbols (const struct block *block, const char *name,
+void iterate_over_symbols (const struct block *block,
+			   const lookup_name_info &name,
 			   const domain_enum domain,
 			   gdb::function_view<symbol_found_callback_ftype> callback);
 
@@ -1728,4 +1983,15 @@ void initialize_objfile_symbol (struct symbol *);
 
 struct template_symbol *allocate_template_symbol (struct objfile *);
 
+/* Test to see if the symbol of language SYMBOL_LANGUAGE specified by
+   SYMNAME (which is already demangled for C++ symbols) matches
+   SYM_TEXT in the first SYM_TEXT_LEN characters.  If so, add it to
+   the current completion list.  */
+void completion_list_add_name (completion_tracker &tracker,
+			       language symbol_language,
+			       const char *symname,
+			       const lookup_name_info &lookup_name,
+			       const char *sym_text, int sym_text_len,
+			       const char *text, const char *word);
+
 #endif /* !defined(SYMTAB_H) */
diff --git a/gdb/testsuite/ChangeLog b/gdb/testsuite/ChangeLog
index d8ed391..f85af9c 100644
--- a/gdb/testsuite/ChangeLog
+++ b/gdb/testsuite/ChangeLog
@@ -1,3 +1,9 @@
+2017-11-08   Pedro Alves  <palves@redhat.com>
+
+	* gdb.ada/complete.exp (p <Exported_Capitalized>): New test.
+	(p Exported_Capitalized): New test.
+	(p exported_capitalized): New test.
+
 2017-11-07  Pedro Alves  <palves@redhat.com>
 
 	* gdb.cp/ena-dis-br-range.exp: Add more tests.
diff --git a/gdb/testsuite/gdb.ada/complete.exp b/gdb/testsuite/gdb.ada/complete.exp
index 906c85a..c3631c7 100644
--- a/gdb/testsuite/gdb.ada/complete.exp
+++ b/gdb/testsuite/gdb.ada/complete.exp
@@ -83,6 +83,16 @@ test_gdb_no_completion "exported"
 test_gdb_complete "<Exported" \
                   "p <Exported_Capitalized>"
 
+# While at it, make sure we can print the symbol too, using the '<'
+# notation.
+gdb_test "p <Exported_Capitalized>" " = 2"
+
+# Confirm that we can't print the symbol without the '<' notation.
+gdb_test "p Exported_Capitalized" \
+    "No definition of \"exported_capitalized\" in current context."
+gdb_test "p exported_capitalized" \
+    "No definition of \"exported_capitalized\" in current context."
+
 # A global symbol, created by the binder, that starts with __gnat...
 test_gdb_complete "__gnat_ada_main_progra" \
                   "p __gnat_ada_main_program_name"
diff --git a/gdb/utils.c b/gdb/utils.c
index f3fc16c..b5c011b 100644
--- a/gdb/utils.c
+++ b/gdb/utils.c
@@ -2156,21 +2156,9 @@ fprintf_symbol_filtered (struct ui_file *stream, const char *name,
     }
 }
 
-/* Modes of operation for strncmp_iw_with_mode.  */
-
-enum class strncmp_iw_mode
-{
-  /* Work like strncmp, while ignoring whitespace.  */
-  NORMAL,
-
-  /* Like NORMAL, but also apply the strcmp_iw hack.  I.e.,
-     string1=="FOO(PARAMS)" matches string2=="FOO".  */
-  MATCH_PARAMS,
-};
-
-/* Helper for strncmp_iw and strcmp_iw.  */
+/* See utils.h.  */
 
-static int
+int
 strncmp_iw_with_mode (const char *string1, const char *string2,
 		      size_t string2_len, strncmp_iw_mode mode)
 {
diff --git a/gdb/utils.h b/gdb/utils.h
index 17d6258..e2fa430 100644
--- a/gdb/utils.h
+++ b/gdb/utils.h
@@ -31,6 +31,29 @@ extern void initialize_utils (void);
 
 extern int sevenbit_strings;
 
+/* Modes of operation for strncmp_iw_with_mode.  */
+
+enum class strncmp_iw_mode
+{
+/* Do a strcmp() type operation on STRING1 and STRING2, ignoring any
+   differences in whitespace.  Returns 0 if they match, non-zero if
+   they don't (slightly different than strcmp()'s range of return
+   values).  */
+  NORMAL,
+
+  /* Like NORMAL, but also apply the strcmp_iw hack.  I.e.,
+     string1=="FOO(PARAMS)" matches string2=="FOO".  */
+  MATCH_PARAMS,
+};
+
+/* Helper for strcmp_iw and strncmp_iw.  Exported so that languages
+   can implement both NORMAL and MATCH_PARAMS variants in a single
+   function and defer part of the work to strncmp_iw_with_mode.  */
+extern int strncmp_iw_with_mode (const char *string1,
+				 const char *string2,
+				 size_t string2_len,
+				 strncmp_iw_mode mode);
+
 /* Do a strncmp() type operation on STRING1 and STRING2, ignoring any
    differences in whitespace.  STRING2_LEN is STRING2's length.
    Returns 0 if STRING1 matches STRING2_LEN characters of STRING2,
