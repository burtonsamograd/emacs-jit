which emacs && echo 'System installed emacs...' && time emacs --batch --load lisp/raytracer.el --eval '(ray-test 1)'

echo 'Emacs JIT...'
time src/emacs --batch --load lisp/raytracer.el --eval '(ray-test 1)'
