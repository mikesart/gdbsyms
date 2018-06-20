
----- Original message -----
From: Fabian Giesen
Subject: Re: Gdb...
Date: Tue, 19 Jun 2018 20:39:17 -0700

I'll start with a data structure answer, but you probably don't need to 
do it to substantially improve the situation. See below.

For data structures, the standard "solve this for good" thing would be a 
radix tree/trie here. This has been reinvented a million times under 
different names, see also "PATRICIA" etc.

The high concept is that you have nodes with up to 256 children each, 
one for each byte value. Then you go

   root->child['f']->child['u']->child['n']

etc. to traverse.

Two issues here:
1. These nodes with 256 child pointers are really fat,
2. You end up with a ton of interior nodes that have exactly one child 
and nothing else, which is obviously wasteful. Creating these is also 
expensive.

So you fix these two "by hand".

The fix for 1) is to have not a single node type, but a few different 
ones, for different numbers of children. So you might have one node type 
for up to 4 children, another for up to 16, and only when it's more than 
16 do you create a node that has the full 256 pointers.

The fix for 2) is to only create nodes for leaves and positions where 
you have seen at least two different values in your data set. ("Path 
compression").

To make this concrete, for your example:

   root->child['f']

doesn't point at a node for 'u' after 'f'; it points at a leaf node. 
That leaf says "for me to match, the remaining characters after the f 
need to be 'unc'".

   root->child['n']

points to an interior node that says "for me to match, the next 
character after the n needs to be 's'", and has two children, one for 
'1' and one for '2'.

After that the child node '1' is gonna say "match ::" and have children 
for 'a' and 'b', which both point to leaves matching '::b::func' and 
'::func', respectively.

This is not just a minor tweak; doing this guarantees that your overall 
node count is limited by the number of strings in your set, instead of 
the total number of characters in those strings.

-

A decent summary of ways to implement this:

     https://db.in.tum.de/~leis/papers/ART.pdf

and a clean C99 implementation:

     https://github.com/armon/libart

[The more oldschool way to do this is with binary trees based on the 
_bits_ where keys differ, and that makes for way less code and smaller 
nodes, but like most trees it's not very cache-friendly.]

-

That's a lot of code though. If you don't want to go quite _that_ full 
hog, note that neither unmodified std::sort nor unmodified binary search 
on strings are a good idea, because in C++ code you'll have lots of 
symbols like

"std::unordered_map<std::basic_string<char, std::char_traits<char>, 
std::allocator<char>>, Foo..."

where everything before the "Foo" just tells you "map of string to 
something" and is complete boilerplate. (Same with namespaces etc.)

If you have 200 map types in your app, you'll have 200 symbols like 
that, each of which will end up right next to each other in the sorted 
list, and the std::sort will end up comparing 40 or 50 identical 
characters every time before it gets anywhere.

For sorting, a good way to fix this is to use a multi-key sort.

     https://en.wikipedia.org/wiki/Multi-key_quicksort

The basic idea is that in any recursive divide-and-conquer sort, if all 
strings in your current subset have the same prefix, there's no point 
comparing these prefix characters against each other.

So instead of comparing whole strings in the sort, you explicitly march 
through the strings character by character. Once you've identified a 
range where all strings inside that range are identical up to position 
d, you stop looking at those earlier characters. This is sort of a 
hybrid between a regular sort and radix sort.

For binary search, it's really nifty:

In addition to the current range [l,r), you also keep track of how many 
characters matched with the prefix when you did the string compare.

"match_l" is how many bytes matched with the search string on the 
compare with the current 'l' element.
"match_r" is how many bytes matched with the search string on the 
current 'r' element.

Then any string in [l,r) is guaranteed to have the same first 
min(match_l, match_r) characters.

That's only slightly more complicated than a regular binary search, and 
works much better when your data has lots of repeated prefixes.

If you were gonna do a patch, I'd try to start there: multi-key sort and 
a string-aware binary search. That improves the pathological (but common 
in this scenario) cases substantially and should be maybe 200 lines of code.

-Fabian

On 6/19/2018 7:27 PM, Michael Sartain wrote:
> Pierre-Loup asked me to look at gdb loading time for large games. 
> Debugging something like cs:go was taking ~13 seconds just to start and 
> break at main.
> 
> Turns out that was the good news as they recently made a change in the 
> latest release (v8.1) that bumps that up to ~26 seconds.
> 
> https://sourceware.org/bugzilla/show_bug.cgi?id=23288
> 
> That checkin was ironically called "Optimize .gdb_index symbol name 
> searching":
> https://sourceware.org/git/gitweb.cgi?p=binutils-gdb.git;a=commit;h=3f563c840a2c891ec2868b3e08bfaecb6f7aa57f
> 
> It adds all the symbols for each name component to a vector, like this:
> 
>        Table Entry       Actual symbol
>        ---------------------------------
>        func              func
>        func              ns1::a::b::func
>        b::func           ns1::a::b::func
>        a::b::func        ns1::a::b::func
>        ns1::a::b::func   ns1::a::b::func
>        func              ns1::b::func
>        b::func           ns1::b::func
>        ns1::b::func      ns1::b::func
>        func              ns2::func
>        ns2::func         ns2::func
> 
> Then calls std::sort on that:
> 
>        Table Entry       Actual symbol
>        ---------------------------------
>        a::b::func        ns1::a::b::func
>        b::func           ns1::a::b::func
>        b::func           ns1::b::func
>        func              func
>        func              ns1::a::b::func
>        func              ns1::b::func
>        func              ns2::func
>        ns1::a::b::func   ns1::a::b::func
>        ns1::b::func      ns1::b::func
>        ns2::func         ns2::func
> 
> They can then binary search for matching symbols, and copy the matches 
> to another vector.
> 
> Going through the symbols looking for the components takes around 10 
> seconds when debugging chromium, and the sort adds another 7 seconds.
> 
> Do any of you have any suggestions for a better data structure for 
> storing these sorted symbols? Their recursive "find_first_component" 
> code, which returns "a::" from "a::b::func", etc is also painfully slow, 
> but I'll take a look at that next.
> 
> No idea if they'll take the patch, or even give a shit, but I figured 
> I'd give it a try at least...
> 
> Thanks much.
>   -Mike

