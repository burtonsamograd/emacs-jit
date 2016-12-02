emacs-jit
---------

An Emacs with a JIT compiler for Emacs Lisp based on libjit.  It
improves the performance of lisp/raytracer.el by about 25%.

Work in progress. Latent bugs and emacs will not build with full
JIT'ing enabled yet.

Usage
-----

Provides a new function 'jit-compile' that takes a function symbol or
a lambda expression as an argument that will jit compile the function.

To enable complete JIT'ing of all Emacs Lisp bytecode (here be
dragons), uncomment the 'jit_hotspot_compile' lines in src/eval.c.

Installation
------------

1) Build and install libjit from git:

git clone git://git.savannah.gnu.org/libjit.git
...

2) Build emacs as usual:

./configure ... && make && make install

Explanation
-----------

I did this work back in 2012, so this is an old emacs version and does
not easily port forward. It is publshed as an example showing how to
create a JIT compiler for Emacs Lisp using a technicque i call
'compiling down the spine'.

The compiler removes the overhead of the jump table used to dispatch
the byte code operations by encapsulating bytecode functionality into
individual functions and converting the bytecode into a linear array
of function calls, precomputing and moving the dispatch overhead a
level of abstration down into the processor rather than in software.

A byte compiled function that is jit-compiled has it's code vector
replaced with a new one containing a single bytecode instruction
Bjitcall followed by the JIT compiled code vector.

The JIT is currently 'working' when used on individual functions with
M-x jit-compile, enough to run the raytracer in lisp/raytracer.el. If
the 'hotspot' (WIP) compiler is enabled by default (see src/eval.c at
jit_hotspot_bytecode) emacs will not build fully, so there are still
lurking bugs in the implementation that need to be squashed.

The 'hotspot' compiler is anything but; it simply compiles the bytecode
on first run for every evaluated piece of code and is a placeholder
for a potential real hotspot compiler in the future.

New or Modified Files of Interest 
---------------------------------

src/bytecode-jit.c: the JIT compiler, included by bytecode.c

src/bytecode.c: minor modifications, original code sources for most of
                bytecode-jit.c

src/eval.c: where the call to the 'hotspot' compiler has been placed,
            search for jit_hotspot_compile to enable it and watch the
            emacs build fail during bytecode compilation.

lisp/raytracer.el: a raytracer in Emacs Lisp used to benchmark the JIT compiler
raytracer.sh

lisp/emacs-lisp/byte-comp.el

--
Burton Samograd
burton.samograd@gmail.com
2016
