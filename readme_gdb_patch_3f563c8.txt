commit 3f563c840a2c891ec2868b3e08bfaecb6f7aa57f
Author: Pedro Alves <palves@redhat.com>
Date:   Wed Nov 8 14:22:32 2017 +0000

    Optimize .gdb_index symbol name searching
    
    As mentioned in the previous patch, .gdb_index name lookup got
    significantly slower with the previous patch.
    
    This patch addresses that, and in the process makes .gdb_index name
    searching faster than what we had before the previous patch, even.
    Using the same test:
    
     $ cat script.cmd
     set pagination off
     set $count = 0
     while $count < 400
       complete b string_prin
       printf "count = %d\n", $count
       set $count = $count + 1
     end
    
     $ time gdb --batch -q ./gdb-with-index -ex "source script.cmd"
    
    I got, before the previous patch (-O2, x86-64):
    
     real    0m1.773s
     user    0m1.737s
     sys     0m0.040s
    
    and after this patch:
    
     real    0m1.361s
     user    0m1.315s
     sys     0m0.040s
    
    The basic idea here is simple: instead of always iterating over all
    the symbol names in the index, we build an accelerator/sorted name
    table and binary search names in it.
    
    Later in the series, we'll want to support wild matching for C++ too,
    so this mechanism already considers that.  For example, say that
    you're looking up functions/methods named "func", no matter the
    containing namespace/class.  If we sorted the table by qualified name,
    then we obviously wouldn't be able to find those symbols with a binary
    search:
    
      func
      ns1::a::b::func
      ns1::b::func
      ns2::func
    
    (function symbol names in .gdb_index have no parameter info, like psymbols)
    
    To address that out, we put an entry for each name component in the
    sorted table.  something like this:
    
      Table Entry       Actual symbol
      ---------------------------------
      func              func
    
      func              ns1::a::b::func
      b::func           ns1::a::b::func
      a::b::func        ns1::a::b::func
      ns1::a::b::func   ns1::a::b::func
    
      func              ns1::b::func
      b::func           ns1::b::func
      ns1::b::func      ns1::b::func
    
      func              ns2::func
      ns2::func         ns2::func
    
    Which sorted results in this:
    
      Table Entry       Actual symbol
      ---------------------------------
      a::b::func        ns1::a::b::func
      b::func           ns1::a::b::func
      b::func           ns1::b::func
      func              func
      func              ns1::a::b::func
      func              ns1::b::func
      func              ns2::func
      ns1::a::b::func   ns1::a::b::func
      ns1::b::func      ns1::b::func
      ns2::func         ns2::func
    
    And we can binary search this.
    
    Note that a binary search approach works for both completion and
    regular lookup, while a name hashing approach only works for normal
    symbol looking, since obviously "fun" and "func" have different
    hashes.
    
    At first I was a bit wary of these tables potentially growing GDB's
    memory significantly.  But I did an experiment that convinced it's not
    a worry at all.  I hacked gdb to count the total number of entries in
    all the tables, attached that gdb to my system/Fedora's Firefox
    (Fedora's debug packages uses .gdb_index), did "set max-completions
    unlimited", and then hit "b [TAB]" to cause everything to expand.
    
    That resulted in 1351355 name_components.  Each entry takes 8 bytes,
    so that's 10810840 bytes (ignoring std::vector overhead), or ~10.3 MB.
    That's IMO too small to worry about, given GDB was using over 7400MB
    total at that point.  I.e., we're talking about 0.1% increase.
    
    dw2_expand_symtabs_matching unit tests covering this will be added in
    a follow up patch.
    
    If the size of this table turns out to be a concern, I have an idea to
    reduce the size of the table further at the expense of a bit more code
    -- the vast majority of the name offsets are either 0 or fit in
    8-bits:
    
     total name_component = 1351355, of which,
     name_component::name_offset instances need  0 bits = 679531
     name_component::name_offset instances need  8 bits = 669526
     name_component::name_offset instances need 16 bits = 2298
     name_component::name_offset instances need 32 bits = 0
     name_component::idx instances need 0 bits  = 51
     name_component::idx instances need 8 bits  = 8361
     name_component::idx instances need 16 bits = 280329
     name_component::idx instances need 32 bits = 1062614
    
    so we could have separate tables for 0 name_offset, 8-bit name_offset
    and 32-bit name_offset.  That'd give us roughly:
    
     679531 * 0 + 669526 * 1 + 2298 * 4 + 1062614 * 4 = 4929174, or ~4.7MB
    
    with only 8-bit and 32-bit tables, that'd be:
    
     1349057 * 1 + 2298 * 4 + 4 * 1351355 = 6763669 bytes, or ~6.5MB.
    
    I don't think we need to bother though.
    
    I also timed:
    
     $ time gdb --batch -q -p `pidof firefox`
     $ time gdb --batch -q -p `pidof firefox` -ex "b main"
     $ time gdb --batch -q -p `pidof firefox` -ex "set max-completion unlimited" -ex "complete b "
    
    and compared before previous patch vs this patch, and I didn't see a
    significant difference, seemingly because time to read debug info
    dominates.  The "complete b " variant of the test takes ~2min
    currently...  (I have a follow up series that speeds that up
    somewhat.)
    
    gdb/ChangeLog:
    2017-11-08  Pedro Alves  <palves@redhat.com>
    
            * dwarf2read.c (byte_swap, MAYBE_SWAP): Move higher up in file.
            (struct name_component): New.
            (mapped_index::name_components): New field.
            (mapped_index::symbol_name_at): New method.
            (dwarf2_read_index): Call mapped_index ctor.
            (dw2_map_matching_symbols): Add comment about name_components
            table.
            (dw2_expand_symtabs_matching): Factor part to...
            (dw2_expand_symtabs_matching_symbol): ... this new function.
            Build name components table, and lookup symbols in it before
            calling the name matcher.
            (dw2_expand_marked_cus): New, factored out from
            dw2_expand_symtabs_matching.
            (dwarf2_per_objfile_free): Call the mapped_index's dtor.

diff --git a/gdb/ChangeLog b/gdb/ChangeLog
index 06ababc..deb1614 100644
--- a/gdb/ChangeLog
+++ b/gdb/ChangeLog
@@ -1,3 +1,20 @@
+2017-11-08  Pedro Alves  <palves@redhat.com>
+
+	* dwarf2read.c (byte_swap, MAYBE_SWAP): Move higher up in file.
+	(struct name_component): New.
+	(mapped_index::name_components): New field.
+	(mapped_index::symbol_name_at): New method.
+	(dwarf2_read_index): Call mapped_index ctor.
+	(dw2_map_matching_symbols): Add comment about name_components
+	table.
+	(dw2_expand_symtabs_matching): Factor part to...
+	(dw2_expand_symtabs_matching_symbol): ... this new function.
+	Build name components table, and lookup symbols in it before
+	calling the name matcher.
+	(dw2_expand_marked_cus): New, factored out from
+	dw2_expand_symtabs_matching.
+	(dwarf2_per_objfile_free): Call the mapped_index's dtor.
+
 2017-11-08   Pedro Alves  <palves@redhat.com>
 
 	* ada-lang.c (ada_encode): Rename to ..
diff --git a/gdb/dwarf2read.c b/gdb/dwarf2read.c
index f37d51f..6f88091 100644
--- a/gdb/dwarf2read.c
+++ b/gdb/dwarf2read.c
@@ -182,6 +182,53 @@ DEF_VEC_I (offset_type);
     GDB_INDEX_CU_SET_VALUE((cu_index), (value)); \
   } while (0)
 
+#if WORDS_BIGENDIAN
+
+/* Convert VALUE between big- and little-endian.  */
+
+static offset_type
+byte_swap (offset_type value)
+{
+  offset_type result;
+
+  result = (value & 0xff) << 24;
+  result |= (value & 0xff00) << 8;
+  result |= (value & 0xff0000) >> 8;
+  result |= (value & 0xff000000) >> 24;
+  return result;
+}
+
+#define MAYBE_SWAP(V)  byte_swap (V)
+
+#else
+#define MAYBE_SWAP(V) static_cast<offset_type> (V)
+#endif /* WORDS_BIGENDIAN */
+
+/* An index into a (C++) symbol name component in a symbol name as
+   recorded in the mapped_index's symbol table.  For each C++ symbol
+   in the symbol table, we record one entry for the start of each
+   component in the symbol in a table of name components, and then
+   sort the table, in order to be able to binary search symbol names,
+   ignoring leading namespaces, both completion and regular look up.
+   For example, for symbol "A::B::C", we'll have an entry that points
+   to "A::B::C", another that points to "B::C", and another for "C".
+   Note that function symbols in GDB index have no parameter
+   information, just the function/method names.  You can convert a
+   name_component to a "const char *" using the
+   'mapped_index::symbol_name_at(offset_type)' method.  */
+
+struct name_component
+{
+  /* Offset in the symbol name where the component starts.  Stored as
+     a (32-bit) offset instead of a pointer to save memory and improve
+     locality on 64-bit architectures.  */
+  offset_type name_offset;
+
+  /* The symbol's index in the symbol and constant pool tables of a
+     mapped_index.  */
+  offset_type idx;
+};
+
 /* A description of the mapped index.  The file format is described in
    a comment by the code that writes the index.  */
 struct mapped_index
@@ -206,6 +253,15 @@ struct mapped_index
 
   /* A pointer to the constant pool.  */
   const char *constant_pool;
+
+  /* The name_component table (a sorted vector).  See name_component's
+     description above.  */
+  std::vector<name_component> name_components;
+
+  /* Convenience method to get at the name of the symbol at IDX in the
+     symbol table.  */
+  const char *symbol_name_at (offset_type idx) const
+  { return this->constant_pool + MAYBE_SWAP (this->symbol_table[idx]); }
 };
 
 typedef struct dwarf2_per_cu_data *dwarf2_per_cu_ptr;
@@ -2160,26 +2216,6 @@ line_header_eq_voidp (const void *item_lhs, const void *item_rhs)
 }
 
 
-#if WORDS_BIGENDIAN
-
-/* Convert VALUE between big- and little-endian.  */
-static offset_type
-byte_swap (offset_type value)
-{
-  offset_type result;
-
-  result = (value & 0xff) << 24;
-  result |= (value & 0xff00) << 8;
-  result |= (value & 0xff0000) >> 8;
-  result |= (value & 0xff000000) >> 24;
-  return result;
-}
-
-#define MAYBE_SWAP(V)  byte_swap (V)
-
-#else
-#define MAYBE_SWAP(V) static_cast<offset_type> (V)
-#endif /* WORDS_BIGENDIAN */
 
 /* Read the given attribute value as an address, taking the attribute's
    form into account.  */
@@ -3444,6 +3480,7 @@ dwarf2_read_index (struct objfile *objfile)
   create_addrmap_from_index (objfile, &local_map);
 
   map = XOBNEW (&objfile->objfile_obstack, struct mapped_index);
+  map = new (map) mapped_index ();
   *map = local_map;
 
   dwarf2_per_objfile->index_table = map;
@@ -4070,7 +4107,11 @@ dw2_map_matching_symbols (struct objfile *objfile,
 
      Since each language has its own symbol name matching algorithm,
      and we don't know which language is the right one, we must match
-     each symbol against all languages.
+     each symbol against all languages.  This would be a potential
+     performance problem if it were not mitigated by the
+     mapped_index::name_components lookup table, which significantly
+     reduces the number of times we need to call into this matcher,
+     making it a non-issue.
 
    - Symbol names in the index have no overload (parameter)
      information.  I.e., in C++, "foo(int)" and "foo(long)" both
@@ -4147,6 +4188,22 @@ gdb_index_symbol_name_matcher::matches (const char *symbol_name)
   return false;
 }
 
+static void
+dw2_expand_marked_cus
+  (mapped_index &index, offset_type idx,
+   struct objfile *objfile,
+   gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
+   search_domain kind);
+
+static void
+dw2_expand_symtabs_matching_symbol
+  (mapped_index &index,
+   const lookup_name_info &lookup_name_in,
+   gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
+   enum search_domain kind,
+   gdb::function_view<void (offset_type)> on_match);
+
 static void
 dw2_expand_symtabs_matching
   (struct objfile *objfile,
@@ -4158,14 +4215,12 @@ dw2_expand_symtabs_matching
 {
   int i;
   offset_type iter;
-  struct mapped_index *index;
 
   dw2_setup (objfile);
 
   /* index_table is NULL if OBJF_READNOW.  */
   if (!dwarf2_per_objfile->index_table)
     return;
-  index = dwarf2_per_objfile->index_table;
 
   if (file_matcher != NULL)
     {
@@ -4239,30 +4294,212 @@ dw2_expand_symtabs_matching
 	}
     }
 
-  gdb_index_symbol_name_matcher lookup_name_matcher (lookup_name);
+  mapped_index &index = *dwarf2_per_objfile->index_table;
 
-  for (iter = 0; iter < index->symbol_table_slots; ++iter)
+  dw2_expand_symtabs_matching_symbol (index, lookup_name,
+				      symbol_matcher,
+				      kind, [&] (offset_type idx)
     {
-      offset_type idx = 2 * iter;
-      const char *name;
-      offset_type *vec, vec_len, vec_idx;
-      int global_seen = 0;
+      dw2_expand_marked_cus (index, idx, objfile, file_matcher,
+			     expansion_notify, kind);
+    });
+}
 
-      QUIT;
+/* Helper for dw2_expand_symtabs_matching that works with a
+   mapped_index instead of the containing objfile.  This is split to a
+   separate function in order to be able to unit test the
+   name_components matching using a mock mapped_index.  For each
+   symbol name that matches, calls MATCH_CALLBACK, passing it the
+   symbol's index in the mapped_index symbol table.  */
 
-      if (index->symbol_table[idx] == 0 && index->symbol_table[idx + 1] == 0)
-	continue;
+static void
+dw2_expand_symtabs_matching_symbol
+  (mapped_index &index,
+   const lookup_name_info &lookup_name,
+   gdb::function_view<expand_symtabs_symbol_matcher_ftype> symbol_matcher,
+   enum search_domain kind,
+   gdb::function_view<void (offset_type)> match_callback)
+{
+  gdb_index_symbol_name_matcher lookup_name_matcher
+    (lookup_name);
+
+  auto *name_cmp = case_sensitivity == case_sensitive_on ? strcmp : strcasecmp;
+
+  /* Build the symbol name component sorted vector, if we haven't yet.
+     The code below only knows how to break apart components of C++
+     symbol names (and other languages that use '::' as
+     namespace/module separator).  If we add support for wild matching
+     to some language that uses some other operator (E.g., Ada, Go and
+     D use '.'), then we'll need to try splitting the symbol name
+     according to that language too.  Note that Ada does support wild
+     matching, but doesn't currently support .gdb_index.  */
+  if (index.name_components.empty ())
+    {
+      for (size_t iter = 0; iter < index.symbol_table_slots; ++iter)
+	{
+	  offset_type idx = 2 * iter;
+
+	  if (index.symbol_table[idx] == 0
+	      && index.symbol_table[idx + 1] == 0)
+	    continue;
+
+	  const char *name = index.symbol_name_at (idx);
+
+	  /* Add each name component to the name component table.  */
+	  unsigned int previous_len = 0;
+	  for (unsigned int current_len = cp_find_first_component (name);
+	       name[current_len] != '\0';
+	       current_len += cp_find_first_component (name + current_len))
+	    {
+	      gdb_assert (name[current_len] == ':');
+	      index.name_components.push_back ({previous_len, idx});
+	      /* Skip the '::'.  */
+	      current_len += 2;
+	      previous_len = current_len;
+	    }
+	  index.name_components.push_back ({previous_len, idx});
+	}
 
-      name = index->constant_pool + MAYBE_SWAP (index->symbol_table[idx]);
+      /* Sort name_components elements by name.  */
+      auto name_comp_compare = [&] (const name_component &left,
+				    const name_component &right)
+	{
+	  const char *left_qualified = index.symbol_name_at (left.idx);
+	  const char *right_qualified = index.symbol_name_at (right.idx);
+
+	  const char *left_name = left_qualified + left.name_offset;
+	  const char *right_name = right_qualified + right.name_offset;
+
+	  return name_cmp (left_name, right_name) < 0;
+	};
+
+      std::sort (index.name_components.begin (),
+		 index.name_components.end (),
+		 name_comp_compare);
+    }
+
+  const char *cplus
+    = lookup_name.cplus ().lookup_name ().c_str ();
 
-      if (!lookup_name_matcher.matches (name)
-	  || (symbol_matcher != NULL && !symbol_matcher (name)))
+  /* Comparison function object for lower_bound that matches against a
+     given symbol name.  */
+  auto lookup_compare_lower = [&] (const name_component &elem,
+				   const char *name)
+    {
+      const char *elem_qualified = index.symbol_name_at (elem.idx);
+      const char *elem_name = elem_qualified + elem.name_offset;
+      return name_cmp (elem_name, name) < 0;
+    };
+
+  /* Comparison function object for upper_bound that matches against a
+     given symbol name.  */
+  auto lookup_compare_upper = [&] (const char *name,
+				   const name_component &elem)
+    {
+      const char *elem_qualified = index.symbol_name_at (elem.idx);
+      const char *elem_name = elem_qualified + elem.name_offset;
+      return name_cmp (name, elem_name) < 0;
+    };
+
+  auto begin = index.name_components.begin ();
+  auto end = index.name_components.end ();
+
+  /* Find the lower bound.  */
+  auto lower = [&] ()
+    {
+      if (lookup_name.completion_mode () && cplus[0] == '\0')
+	return begin;
+      else
+	return std::lower_bound (begin, end, cplus, lookup_compare_lower);
+    } ();
+
+  /* Find the upper bound.  */
+  auto upper = [&] ()
+    {
+      if (lookup_name.completion_mode ())
+	{
+	  /* The string frobbing below won't work if the string is
+	     empty.  We don't need it then, anyway -- if we're
+	     completing an empty string, then we want to iterate over
+	     the whole range.  */
+	  if (cplus[0] == '\0')
+	    return end;
+
+	  /* In completion mode, increment the last character because
+	     we want UPPER to point past all symbols names that have
+	     the same prefix.  */
+	  std::string after = cplus;
+
+	  gdb_assert (after.back () != 0xff);
+	  after.back ()++;
+
+	  return std::upper_bound (lower, end, after.c_str (),
+				   lookup_compare_upper);
+	}
+      else
+	return std::upper_bound (lower, end, cplus, lookup_compare_upper);
+    } ();
+
+  /* Now for each symbol name in range, check to see if we have a name
+     match, and if so, call the MATCH_CALLBACK callback.  */
+
+  /* The same symbol may appear more than once in the range though.
+     E.g., if we're looking for symbols that complete "w", and we have
+     a symbol named "w1::w2", we'll find the two name components for
+     that same symbol in the range.  To be sure we only call the
+     callback once per symbol, we first collect the symbol name
+     indexes that matched in a temporary vector and ignore
+     duplicates.  */
+  std::vector<offset_type> matches;
+  matches.reserve (std::distance (lower, upper));
+
+  for (;lower != upper; ++lower)
+    {
+      const char *qualified = index.symbol_name_at (lower->idx);
+
+      if (!lookup_name_matcher.matches (qualified)
+	  || (symbol_matcher != NULL && !symbol_matcher (qualified)))
 	continue;
 
-      /* The name was matched, now expand corresponding CUs that were
-	 marked.  */
-      vec = (offset_type *) (index->constant_pool
-			     + MAYBE_SWAP (index->symbol_table[idx + 1]));
+      matches.push_back (lower->idx);
+    }
+
+  std::sort (matches.begin (), matches.end ());
+
+  /* Finally call the callback, once per match.  */
+  ULONGEST prev = -1;
+  for (offset_type idx : matches)
+    {
+      if (prev != idx)
+	{
+	  match_callback (idx);
+	  prev = idx;
+	}
+    }
+
+  /* Above we use a type wider than idx's for 'prev', since 0 and
+     (offset_type)-1 are both possible values.  */
+  static_assert (sizeof (prev) > sizeof (offset_type), "");
+}
+
+/* Helper for dw2_expand_matching symtabs.  Called on each symbol
+   matched, to expand corresponding CUs that were marked.  IDX is the
+   index of the symbol name that matched.  */
+
+static void
+dw2_expand_marked_cus
+  (mapped_index &index, offset_type idx,
+   struct objfile *objfile,
+   gdb::function_view<expand_symtabs_file_matcher_ftype> file_matcher,
+   gdb::function_view<expand_symtabs_exp_notify_ftype> expansion_notify,
+   search_domain kind)
+{
+  const char *name;
+  offset_type *vec, vec_len, vec_idx;
+  bool global_seen = false;
+
+      vec = (offset_type *) (index.constant_pool
+			     + MAYBE_SWAP (index.symbol_table[idx + 1]));
       vec_len = MAYBE_SWAP (vec[0]);
       for (vec_idx = 0; vec_idx < vec_len; ++vec_idx)
 	{
@@ -4278,7 +4515,7 @@ dw2_expand_symtabs_matching
 	     and indices >= 7 may elide them for certain symbols
 	     (gold does this).  */
 	  int attrs_valid =
-	    (index->version >= 7
+	    (index.version >= 7
 	     && symbol_kind != GDB_INDEX_SYMBOL_KIND_NONE);
 
 	  /* Work around gold/15646.  */
@@ -4287,7 +4524,7 @@ dw2_expand_symtabs_matching
 	      if (!is_static && global_seen)
 		continue;
 	      if (!is_static)
-		global_seen = 1;
+		global_seen = true;
 	    }
 
 	  /* Only check the symbol's kind if it has one.  */
@@ -4338,7 +4575,6 @@ dw2_expand_symtabs_matching
 		}
 	    }
 	}
-    }
 }
 
 /* A helper for dw2_find_pc_sect_compunit_symtab which finds the most specific
@@ -23362,6 +23598,9 @@ dwarf2_per_objfile_free (struct objfile *objfile, void *d)
 
   if (data->dwz_file && data->dwz_file->dwz_bfd)
     gdb_bfd_unref (data->dwz_file->dwz_bfd);
+
+  if (data->index_table != NULL)
+    data->index_table->~mapped_index ();
 }
 
 
