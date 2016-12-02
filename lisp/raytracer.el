(require 'cl)

(defun sq (x)
  (* x x))

(defun mag (x y z)
  (sqrt (+ (sq x) (sq y) (sq z))))

(defun unit-vector (x y z)
  (let ((d (mag x y z)))
    (values (/ x d) (/ y d) (/ z d))))

(defstruct (point (:conc-name nil)) x y z)

(defun distance (p1 p2)
  (mag (- (x p1) (x p2))
       (- (y p1) (y p2))
       (- (z p1) (z p2))))

(defun minroot (a b c)
  (if (zerop a)
      (/ (- c) b)
    (let ((disc (- (sq b) (* 4 a c))))
      (unless (minusp disc)
        (let ((discrt (sqrt disc)))
          (min (/ (+ (- b) discrt) (* 2 a))
               (/ (- (- b) discrt) (* 2 a))))))))

(defstruct surface color)

(defvar *world* nil)
(defvar *eye* nil)

(defvar *xres* 256)
(defvar *yres* 256)

(defun tracer (buffer-name res)
  (save-excursion
    (ignore-errors
      (kill-buffer buffer-name))
    (switch-to-buffer buffer-name)
    (fundamental-mode)
    (insert (format "P2 %s %s 255" (* res *xres*) (* res *xres*)))
    (newline)
    (let ((inc (/ 1.0 res)))
      (do ((y (- (/ *yres* 2)) (+ y inc)))
          ((< (- (/ *yres* 2) y) inc))
        (do ((x (- (/ *xres* 2)) (+ x inc)))
            ((< (- (/ *xres* 2) x) inc))
          (insert (format "%s " (color-at x y))))))
    (set-visited-file-name buffer-name)
    (save-buffer)))

(defun color-at (x y)
  (multiple-value-bind (xr yr zr)
      (unit-vector (- x (x *eye*))
                   (- y (y *eye*))
                   (- 0 (z *eye*)))
    (round (* (send-ray *eye* xr yr zr) 255))))

(defun send-ray (pt xr yr zr)
  (multiple-value-bind (s int) (first-hit pt xr yr zr)
    (if s
        (* (lambert s int xr yr zr) (surface-color s))
      0)))

(defun first-hit (pt xr yr zr)
  (let (surface hit dist)
    (dolist (s *world*)
      (let ((h (intersect s pt xr yr zr)))
        (when h
          (let ((d (distance h pt)))
            (when (or (null dist) (< d dist))
              (setf surface s
                    hit h
                    dist d))))))
    (values surface hit)))

(defun lambert (s int xr yr zr)
  (multiple-value-bind (xn yn zn) (normal s int)
    (max 0 (+ (* xr xn) (* yr yn) (* zr zn)))))

(defstruct (sphere (:include surface))
  radius center)

(defun defsphere (x y z r c)
  (let ((s (make-sphere
            :radius r
            :center (make-point :x x :y y :z z)
            :color c)))
    (push s *world*)))

(defun intersect (s pt xr yr zr)
  (funcall (typecase s (sphere #'sphere-intersect))
           s pt xr yr zr))

(defun sphere-intersect (s pt xr yr zr)
  (let* ((c (sphere-center s))
         (n (minroot (+ (sq xr) (sq yr) (sq zr))
                     (* 2 (+ (* (- (x pt) (x c)) xr)
                             (* (- (y pt) (y c)) yr)
                             (* (- (z pt) (z c)) zr)))
                     (+ (sq (- (x pt) (x c)))
                        (sq (- (y pt) (y c)))
                        (sq (- (z pt) (z c)))
                        (- (sq (sphere-radius s)))))))
    (if n
        (make-point :x (+ (x pt) (* n xr))
                    :y (+ (y pt) (* n yr))
                    :z (+ (z pt) (* n zr))))))

(defun normal (s pt)
  (funcall (typecase s (sphere #'sphere-normal))
           s pt))

(defun sphere-normal (s pt)
  (let ((c (sphere-center s)))
    (unit-vector (- (x c) (x pt))
                 (- (y c) (y pt)) 
                 (- (z c) (z pt)))))

(defun ray-test (res)
  (setf *world* nil)
  (setf *eye* (make-point :x 0.0 :y 0.0 :z 200.0))
  (defsphere 0 -300 -1200 200 .8)
  (defsphere -80 -150 -1200 200 .7)
  (defsphere 70 -100 -1200 200 .9)
  (do ((x -3 (1+ x)))
      ((> x 3))
    (do ((z 2 (1+ z)))
        ((> z 9))
      (defsphere (* x 200) 300 (* z -400) 40 .75)))
  (tracer "image.pbm" res))

;(byte-compile 'distance)
;(message (format "%s" (length (aref (symbol-function 'distance) 1))))
;(save-excursion
;  (switch-to-buffer "x")
;  (disassemble 'distance "x")
;  (insert (format "%s" (aref (aref (symbol-function 'distance) 1) 29)))
;  (set-visited-file-name "d")
;  (save-buffer))
(defun f (x) (if x (* x x) nil))
(if (fboundp 'jit-compile)
    (defmacro jit (f)
      `(progn
	 ;;     (jit-compile ,f)
	 ;;     (trace-function ,f)
	 ))
    (defmacro jit (f)))

(jit 'f)
(jit 'minroot)
(jit 'sq)
(jit 'mag)
(jit 'unit-vector)
(jit 'distance)
(jit 'tracer)
(jit 'color-at)
(jit 'send-ray)
(jit 'first-hit)
(jit 'lambert)
(jit 'defsphere)
(jit 'intersect)
(jit 'sphere-intersect)
(jit 'normal)
(jit 'sphere-normal)
(jit 'ray-test)
