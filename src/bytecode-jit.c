#include <stdio.h>

static jit_context_t bytecode_jit_context;
static jit_type_t bytecode_jitcall_signature;
static jit_type_t bytecode_jit_core_signature;
static jit_type_t bytecode_jit_goto_signature;
static jit_type_t bytecode_jit_op_signature;
static jit_type_t bytecode_jit_vector_ref_signature;
#ifdef JIT_DEBUG
static jit_type_t fprintf_sig;
#endif
/* Push x onto the execution stack.  This used to be #define PUSH(x)
   (*++stackp = (x)) This oddity is necessary because Alliant can't be
   bothered to compile the preincrement operator properly, as of 4/91.
   -JimB */

#define JIT_PUSH(x) (core.top++, *core.top = (x))
#define _JIT_PUSH(x) (core->top++, *core->top = (x))

/* Pop a value off the execution stack.  */

#define JIT_POP (*core.top--)
#define _JIT_POP (*core->top--)

/* Discard n values from the execution stack.  */

#define JIT_DISCARD(n) (core.top -= (n))
#define _JIT_DISCARD(n) (core->top -= (n))

/* Get the value which is at the top of the execution stack, but don't
   pop it. */

#define JIT_TOP (*core.top)
#define _JIT_TOP (*core->top)

/* Actions that must be performed before and after calling a function
   that might GC.  */

#if !BYTE_MAINTAIN_TOP
#define BEFORE_POTENTIAL_GC()	((void)0)
#define _BEFORE_POTENTIAL_GC()	((void)0)
#define AFTER_POTENTIAL_GC()	((void)0)
#define _AFTER_POTENTIAL_GC()	((void)0)
#else
#define BEFORE_POTENTIAL_GC()	core.stack.top = top
#define _BEFORE_POTENTIAL_GC()	core->stack.top = top
#define AFTER_POTENTIAL_GC()	core.stack.top = NULL
#define _AFTER_POTENTIAL_GC()	core->stack.top = NULL
#endif

/* Garbage collect if we have consed enough since the last time.
   We do this at every branch, to avoid loops that never GC.  */

#define MAYBE_GC()		\
  do {				\
   BEFORE_POTENTIAL_GC ();	\
   maybe_gc ();			\
   AFTER_POTENTIAL_GC ();	\
 } while (0)

#define _MAYBE_GC()		\
  do {				\
   _BEFORE_POTENTIAL_GC ();	\
   maybe_gc ();			\
   _AFTER_POTENTIAL_GC ();	\
 } while (0)

#define JIT_USE_FASTCALL
#ifdef JIT_USE_FASTCALL
#define JIT_FASTCALL __attribute__((fastcall))
#else
#define JIT_FASTCALL
#endif

#define BYTECODE_JIT(f) \
  JIT_FASTCALL static void bytecode_jit_##f(struct bytecode_jit_core* core)
#define BYTECODE_JIT_OP(f) \
  JIT_FASTCALL static void bytecode_jit_##f(struct bytecode_jit_core* core, int op)
#define BYTECODE_JIT_GOTO(f) \
  JIT_FASTCALL static int bytecode_jit_##f(struct bytecode_jit_core* core)
#define BYTECODE_JIT_VECTOR_REF(f) \
  JIT_FASTCALL static void bytecode_jit_##f(struct bytecode_jit_core* core, Lisp_Object v)

BYTECODE_JIT_VECTOR_REF(varref)
{
  //  fprintf(stderr, "varref 1 core->stack: 0x%x\n", core->pstack);
  Lisp_Object v1;
  if (SYMBOLP (v))
    {
      if (XSYMBOL (v)->redirect != SYMBOL_PLAINVAL
          || (v1 = SYMBOL_VAL (XSYMBOL (v)),
              EQ (v1, Qunbound)))
        {
          _BEFORE_POTENTIAL_GC ();
          v1 = Fsymbol_value (v);
          _AFTER_POTENTIAL_GC ();
        }
    }
  else
    {
      _BEFORE_POTENTIAL_GC ();
      v1 = Fsymbol_value (v);
      _AFTER_POTENTIAL_GC ();
    }
  _JIT_PUSH (v1);
  //  fprintf(stderr, "varref core: 0x%x\n", core);
  //  fprintf(stderr, "varref 2 core->stack: 0x%x\n", core->pstack);
  //  fprintf(stderr, "varref top: 0x%x\n", core->top[0]);
}

BYTECODE_JIT(car)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  if (CONSP (v1))
    _JIT_TOP = XCAR (v1);
  else if (NILP (v1))
    _JIT_TOP = Qnil;
  else
    {
      _BEFORE_POTENTIAL_GC ();
      wrong_type_argument (Qlistp, v1);
      _AFTER_POTENTIAL_GC ();
    }
}

BYTECODE_JIT(eq)
{
  Lisp_Object v1;
  v1 = _JIT_POP;
  _JIT_TOP = EQ (v1, _JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT(memq)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fmemq (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(cdr)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  if (CONSP (v1))
    _JIT_TOP = XCDR (v1);
  else if (NILP (v1))
    _JIT_TOP = Qnil;
  else
    {
      _BEFORE_POTENTIAL_GC ();
      wrong_type_argument (Qlistp, v1);
      _AFTER_POTENTIAL_GC ();
    }
}

BYTECODE_JIT_VECTOR_REF(varset)
{
  Lisp_Object sym, val;

  // TODO: clean this up with sym -> v
  sym = v;
  val = _JIT_TOP;
  
  /* Inline the most common case.  */
  if (SYMBOLP (sym)
      && !EQ (val, Qunbound)
      && !XSYMBOL (sym)->redirect
      && !SYMBOL_CONSTANT_P (sym))
    SET_SYMBOL_VAL (XSYMBOL (sym), val);
  else
    {
      _BEFORE_POTENTIAL_GC ();
      set_internal (sym, val, Qnil, 0);
      _AFTER_POTENTIAL_GC ();
    }
  (void) _JIT_POP;
}

BYTECODE_JIT(dup)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  _JIT_PUSH (v1);
}

BYTECODE_JIT_VECTOR_REF(varbind)
{
  /* Specbind can signal and thus GC.  */
  _BEFORE_POTENTIAL_GC ();
  specbind (v, _JIT_POP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT_OP(call)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (op);
  int i;
  //  fprintf(stderr, "call core: 0x%x\n", core);
  //  fprintf(stderr, "call core->stack: 0x%x\n", core->pstack);
  //for(i = 0; i < op +1 ; i++) {
  //fprintf(stderr, "call stack+%d: 0x%x\n", i, core->top[i]);
  //}
  _JIT_TOP = Ffuncall (op + 1, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT_OP(unbind)
{
  _BEFORE_POTENTIAL_GC ();
  unbind_to (SPECPDL_INDEX () - op, Qnil);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(unbind_all)
{
  /* To unbind back to the beginning of this frame.  Not used yet,
     but will be needed for tail-recursion elimination.  */
  _BEFORE_POTENTIAL_GC ();
  unbind_to (core->count, Qnil);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(goto)
{
  _MAYBE_GC ();
  BYTE_CODE_QUIT;
}

BYTECODE_JIT_GOTO(gotoifnil)
{
  Lisp_Object v1;
  _MAYBE_GC ();
  v1 = _JIT_POP;
  if (NILP (v1))
    {
      BYTE_CODE_QUIT;
      return 1;
    }
  return 0;
}

BYTECODE_JIT_GOTO(gotoifnonnil)
{
  Lisp_Object v1;
  _MAYBE_GC ();
  v1 = _JIT_POP;
  if (!NILP (v1))
    {
      BYTE_CODE_QUIT;
      return 1;
    }
  return 0;
}

BYTECODE_JIT_GOTO(gotoifnilelsepop)
{
  _MAYBE_GC ();
  if (NILP (_JIT_TOP))
    {
      BYTE_CODE_QUIT;
      return 1;
    }
  else _JIT_DISCARD (1);
  return 0;
}

BYTECODE_JIT_GOTO(gotoifnonnilelsepop)
{
  MAYBE_GC ();
  if (!NILP (_JIT_TOP))
    {
      BYTE_CODE_QUIT;
      return 1;
    }
  else _JIT_DISCARD (1);
  return 0;
}

BYTECODE_JIT_GOTO(Rgoto)
{
  _MAYBE_GC ();
  BYTE_CODE_QUIT;
  return 1;
  //core->stack.pc += (int) *core->stack.pc - 127;
}

BYTECODE_JIT_GOTO(Rgotoifnil)
{
  Lisp_Object v1;
  _MAYBE_GC ();
  v1 = _JIT_POP;
  if (NILP (v1))
    {
      BYTE_CODE_QUIT;
      return 1;
      //core->stack.pc += (int) *core->stack.pc - 128;
    }
  // FIXME: this is wierd
  //core->stack.pc++;
}

BYTECODE_JIT_GOTO(Rgotoifnonnil)
{
  Lisp_Object v1;
  _MAYBE_GC ();
  v1 = _JIT_POP;
  if (!NILP (v1))
    {
      BYTE_CODE_QUIT;
      return 1;
      //core->stack.pc += (int) *core->stack.pc - 128;
    }
  //core->stack.pc++;
  return 0;
}

BYTECODE_JIT_GOTO(Rgotoifnilelsepop)
{
  _MAYBE_GC ();
  //op = *core->stack.pc++;
  if (NILP (_JIT_TOP))
    {
      BYTE_CODE_QUIT;
      return 1;
      //core->stack.pc += op - 128;
    }
  else _JIT_DISCARD (1);
  return 0;
}

BYTECODE_JIT_GOTO(Rgotoifnonnilelsepop)
{
  _MAYBE_GC ();
  int op = *core->pstack->pc++;
  if (!NILP (_JIT_TOP))
    {
      BYTE_CODE_QUIT;
      return 1;
      //core->stack.pc += op - 128;
    }
  else _JIT_DISCARD (1);
  return 0;
}

BYTECODE_JIT(discard)
{
  _JIT_DISCARD (1);
}

BYTECODE_JIT_VECTOR_REF(constant2)
{
  _JIT_PUSH (v);
}

BYTECODE_JIT(save_excursion)
{
  record_unwind_protect (save_excursion_restore,
                         save_excursion_save ());
}

BYTECODE_JIT(save_current_buffer)
{
  record_unwind_current_buffer ();
}

BYTECODE_JIT(save_window_excursion)
{
  register ptrdiff_t count1 = SPECPDL_INDEX ();
  record_unwind_protect (Fset_window_configuration,
                         Fcurrent_window_configuration (Qnil));
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fprogn (_JIT_TOP);
  unbind_to (count1, _JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(save_restriction)
{
  record_unwind_protect (save_restriction_restore,
                         save_restriction_save ());
}

BYTECODE_JIT(catch)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = internal_catch (_JIT_TOP, eval_sub, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(unwind_protect)
{
  record_unwind_protect (Fprogn, _JIT_POP);
}

BYTECODE_JIT(condition_case)
{
  Lisp_Object handlers, body;
  handlers = _JIT_POP;
  body = _JIT_POP;
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = internal_lisp_condition_case (_JIT_TOP, body, handlers);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(temp_output_buffer_setup)
{
  _BEFORE_POTENTIAL_GC ();
  CHECK_STRING (_JIT_TOP);
  temp_output_buffer_setup (SSDATA (_JIT_TOP));
  _AFTER_POTENTIAL_GC ();
  _JIT_TOP = Vstandard_output;
}

BYTECODE_JIT(temp_output_buffer_show)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  temp_output_buffer_show (_JIT_TOP);
  _JIT_TOP = v1;
  /* pop binding of standard-output */
  unbind_to (SPECPDL_INDEX () - 1, Qnil);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(nth)
{
  Lisp_Object v1, v2;
  EMACS_INT n;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  v2 = _JIT_TOP;
  CHECK_NUMBER (v2);
  n = XINT (v2);
  immediate_quit = 1;
  while (--n >= 0 && CONSP (v1))
    v1 = XCDR (v1);
  immediate_quit = 0;
  _JIT_TOP = CAR (v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(symbolp)
{
  _JIT_TOP = SYMBOLP (_JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT(consp)
{
  _JIT_TOP = CONSP (_JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT(stringp)
{
  _JIT_TOP = STRINGP (_JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT(listp)
{
  _JIT_TOP = CONSP (_JIT_TOP) || NILP (_JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT(not)
{
  _JIT_TOP = NILP (_JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT(cons)
{
  Lisp_Object v1;
  v1 = _JIT_POP;
  _JIT_TOP = Fcons (_JIT_TOP, v1);
}

BYTECODE_JIT(list1)
{
  _JIT_TOP = Fcons (_JIT_TOP, Qnil);
}

BYTECODE_JIT(list2)
{
  Lisp_Object v1;
  v1 = _JIT_POP;
  _JIT_TOP = Fcons (_JIT_TOP, Fcons (v1, Qnil));
}

BYTECODE_JIT(list3)
{
  _JIT_DISCARD (2);
  _JIT_TOP = Flist (3, &_JIT_TOP);
}

BYTECODE_JIT(list4)
{
  _JIT_DISCARD (3);
  _JIT_TOP = Flist (4, &_JIT_TOP);
}

BYTECODE_JIT_OP(listN)
{
  _JIT_DISCARD (op - 1);
  _JIT_TOP = Flist (op, &_JIT_TOP);
}

BYTECODE_JIT(length)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Flength (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(aref)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Faref (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(aset)
{
  Lisp_Object v1, v2;
  _BEFORE_POTENTIAL_GC ();
  v2 = _JIT_POP; v1 = _JIT_POP;
  _JIT_TOP = Faset (_JIT_TOP, v1, v2);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(symbol_value)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fsymbol_value (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(symbol_function)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fsymbol_function (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(set)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fset (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(fset)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Ffset (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(get)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fget (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(substring)
{
  Lisp_Object v1, v2;
  _BEFORE_POTENTIAL_GC ();
  v2 = _JIT_POP; v1 = _JIT_POP;
  _JIT_TOP = Fsubstring (_JIT_TOP, v1, v2);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(concat2)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fconcat (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(concat3)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (2);
  _JIT_TOP = Fconcat (3, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(concat4)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (3);
  _JIT_TOP = Fconcat (4, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT_OP(concatN)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (op - 1);
  _JIT_TOP = Fconcat (op, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(sub1)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  if (INTEGERP (v1))
    {
      XSETINT (v1, XINT (v1) - 1);
      _JIT_TOP = v1;
    }
  else
    {
      _BEFORE_POTENTIAL_GC ();
      _JIT_TOP = Fsub1 (v1);
      _AFTER_POTENTIAL_GC ();
    }
}

BYTECODE_JIT(add1)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  if (INTEGERP (v1))
    {
      XSETINT (v1, XINT (v1) + 1);
      _JIT_TOP = v1;
    }
  else
    {
      _BEFORE_POTENTIAL_GC ();
      _JIT_TOP = Fadd1 (v1);
      _AFTER_POTENTIAL_GC ();
    }
}

BYTECODE_JIT(eqlsign)
{
  Lisp_Object v1, v2;
  _BEFORE_POTENTIAL_GC ();
  v2 = _JIT_POP; v1 = _JIT_TOP;
  CHECK_NUMBER_OR_FLOAT_COERCE_MARKER (v1);
  CHECK_NUMBER_OR_FLOAT_COERCE_MARKER (v2);
  _AFTER_POTENTIAL_GC ();
  if (FLOATP (v1) || FLOATP (v2))
    {
      double f1, f2;
      
      f1 = (FLOATP (v1) ? XFLOAT_DATA (v1) : XINT (v1));
      f2 = (FLOATP (v2) ? XFLOAT_DATA (v2) : XINT (v2));
      _JIT_TOP = (f1 == f2 ? Qt : Qnil);
    }
  else
    _JIT_TOP = (XINT (v1) == XINT (v2) ? Qt : Qnil);
}

BYTECODE_JIT(gtr)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fgtr (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(lss)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Flss (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(leq)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fleq (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(geq)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fgeq (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(diff)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fminus (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(negate)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  if (INTEGERP (v1))
    {
      XSETINT (v1, - XINT (v1));
      _JIT_TOP = v1;
    }
  else
    {
      _BEFORE_POTENTIAL_GC ();
      _JIT_TOP = Fminus (1, &_JIT_TOP);
      _AFTER_POTENTIAL_GC ();
    }
}

BYTECODE_JIT(plus)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fplus (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(max)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fmax (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(min)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fmin (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(mult)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Ftimes (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(quo)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fquo (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(rem)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Frem (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(point)
{
  Lisp_Object v1;
  XSETFASTINT (v1, PT);
  _JIT_PUSH (v1);
}

BYTECODE_JIT(goto_char)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fgoto_char (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(insert)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Finsert (1, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT_OP(insertN)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (op - 1);
  _JIT_TOP = Finsert (op, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(point_max)
{
  Lisp_Object v1;
  XSETFASTINT (v1, ZV);
  _JIT_PUSH (v1);
}

BYTECODE_JIT(point_min)
{
  Lisp_Object v1;
  XSETFASTINT (v1, BEGV);
  _JIT_PUSH (v1);
}

BYTECODE_JIT(char_after)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fchar_after (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(following_char)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = Ffollowing_char ();
  _AFTER_POTENTIAL_GC ();
  _JIT_PUSH (v1);
}

BYTECODE_JIT(preceding_char)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = Fprevious_char ();
  _AFTER_POTENTIAL_GC ();
  _JIT_PUSH (v1);
}

BYTECODE_JIT(current_column)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  XSETFASTINT (v1, current_column ());
  _AFTER_POTENTIAL_GC ();
  _JIT_PUSH (v1);
}

BYTECODE_JIT(indent_to)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Findent_to (_JIT_TOP, Qnil);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(eolp)
{
  _JIT_PUSH (Feolp ());
}

BYTECODE_JIT(eobp)
{
  _JIT_PUSH (Feobp ());
}

BYTECODE_JIT(bolp)
{
  _JIT_PUSH (Fbolp ());
}

BYTECODE_JIT(bobp)
{
  _JIT_PUSH (Fbobp ());
}

BYTECODE_JIT(current_buffer)
{
  _JIT_PUSH (Fcurrent_buffer ());
}

BYTECODE_JIT(set_buffer)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fset_buffer (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(interactive_p)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_PUSH (call0 (intern ("interactive-p")));
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(forward_char)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fforward_char (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(forward_word)
{
  BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fforward_word (_JIT_TOP);
  AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(skip_chars_forward)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fskip_chars_forward (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(skip_chars_backward)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fskip_chars_backward (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(forward_line)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fforward_line (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(char_syntax)
{
  int c;
  
  _BEFORE_POTENTIAL_GC ();
  CHECK_CHARACTER (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
  c = XFASTINT (_JIT_TOP);
  if (NILP (BVAR (current_buffer, enable_multibyte_characters)))
    MAKE_CHAR_MULTIBYTE (c);
  XSETFASTINT (_JIT_TOP, syntax_code_spec[(int) SYNTAX (c)]);
}

BYTECODE_JIT(buffer_substring)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fbuffer_substring (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(delete_region)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fdelete_region (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(narrow_to_region)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fnarrow_to_region (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(widen)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_PUSH (Fwiden ());
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(end_of_line)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fend_of_line (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(set_marker)
{
  Lisp_Object v1, v2;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  v2 = _JIT_POP;
  _JIT_TOP = Fset_marker (_JIT_TOP, v2, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(match_beginning)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fmatch_beginning (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(match_end)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fmatch_end (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(upcase)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fupcase (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(downcase)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fdowncase (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(stringeqlsign)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fstring_equal (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(stringlss)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fstring_lessp (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(equal)
{
  Lisp_Object v1;
  v1 = _JIT_POP;
  _JIT_TOP = Fequal (_JIT_TOP, v1);
}

BYTECODE_JIT(nthcdr)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fnthcdr (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(elt)
{
  Lisp_Object v1, v2;
  if (CONSP (_JIT_TOP))
    {
      /* Exchange args and then do nth.  */
      EMACS_INT n;
      _BEFORE_POTENTIAL_GC ();
      v2 = _JIT_POP;
      v1 = _JIT_TOP;
      CHECK_NUMBER (v2);
      _AFTER_POTENTIAL_GC ();
      n = XINT (v2);
      immediate_quit = 1;
      while (--n >= 0 && CONSP (v1))
        v1 = XCDR (v1);
      immediate_quit = 0;
      _JIT_TOP = CAR (v1);
    }
  else
    {
      _BEFORE_POTENTIAL_GC ();
      v1 = _JIT_POP;
      _JIT_TOP = Felt (_JIT_TOP, v1);
      _AFTER_POTENTIAL_GC ();
    }
}

BYTECODE_JIT(member)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fmember (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(assq)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fassq (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(nreverse)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_TOP = Fnreverse (_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(setcar)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fsetcar (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(setcdr)
{
  Lisp_Object v1;
  _BEFORE_POTENTIAL_GC ();
  v1 = _JIT_POP;
  _JIT_TOP = Fsetcdr (_JIT_TOP, v1);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(car_safe)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  _JIT_TOP = CAR_SAFE (v1);
}

BYTECODE_JIT(cdr_safe)
{
  Lisp_Object v1;
  v1 = _JIT_TOP;
  _JIT_TOP = CDR_SAFE (v1);
}

BYTECODE_JIT(nconc)
{
  _BEFORE_POTENTIAL_GC ();
  _JIT_DISCARD (1);
  _JIT_TOP = Fnconc (2, &_JIT_TOP);
  _AFTER_POTENTIAL_GC ();
}

BYTECODE_JIT(numberp)
{
  _JIT_TOP = (NUMBERP (_JIT_TOP) ? Qt : Qnil);
}

BYTECODE_JIT(integerp)
{
  _JIT_TOP = INTEGERP (_JIT_TOP) ? Qt : Qnil;
}

BYTECODE_JIT_OP(stack_ref)
{
  Lisp_Object *ptr = core->top - op;
  _JIT_PUSH (*ptr);
}

BYTECODE_JIT_OP(stack_set)
/* stack-set-0 = discard; stack-set-1 = discard-1-preserve-tos.  */
{
  Lisp_Object *ptr = core->top - (op);
  *ptr = _JIT_POP;
}

BYTECODE_JIT_OP(stack_set2)
{
  Lisp_Object *ptr = core->top - (op);
  *ptr = _JIT_POP;
}

BYTECODE_JIT_OP(discardN)
{
  if (op & 0x80)
    {
      op &= 0x7F;
      core->top[-op] = _JIT_TOP;
    }
  _JIT_DISCARD (op);
}

BYTECODE_JIT_VECTOR_REF(constant)
{
  //  fprintf(stderr, "constant 1 core->stack: 0x%x\n", core->pstack);
  _JIT_PUSH (v);
  //  fprintf(stderr, "constant core: 0x%x\n", core);
  //fprintf(stderr, "constant 2 core->stack: 0x%x\n", core->pstack);
  //fprintf(stderr, "constant top: 0x%x\n", core->top[0]);
}

BYTECODE_JIT(return)
{
  //fprintf(stderr, "\n");
  core->result = _JIT_POP;
}

BYTECODE_JIT(jitcall)
{
  /* Pointer to jit function is after this instruction */
  jit_function_t code = *(jit_function_t*)(core->pstack->pc);
  
  /* Function signature is void f(jit_core*); */
  void* args[1] = { &core };
  jit_function_apply(code, args, NULL);

  /* Update the pc */
  core->pstack->pc += sizeof(jit_function_t);
}

Lisp_Object jit_bytecode_compile(Lisp_Object fun) {
  Lisp_Object bytestr = AREF (fun, COMPILED_BYTECODE);
  Lisp_Object vector = AREF (fun, COMPILED_CONSTANTS);
  Lisp_Object maxdepth = AREF (fun, COMPILED_STACK_DEPTH);
  unsigned char* cur_insn;
  int op, bytes_fetched = 0;
  int bytestr_length;

  CHECK_STRING (bytestr);
  CHECK_VECTOR (vector);
  CHECK_NATNUM (maxdepth);

  //Lisp_Object args2[2];
  //  args2[0] = build_string("vector: %s\n");
  //args2[1] = vector;
  //Fmessage(2, args2);

  unsigned char *bytecode_start = cur_insn = SDATA(bytestr);
  if(*bytecode_start == Bjitcall) {
      /* Already JIT compiled */
      return Qt;
  }
          
  //if (STRING_MULTIBYTE (bytestr))
  //  bytestr = Fstring_as_unibyte (bytestr);
  bytestr_length = SBYTES (bytestr);

#define _FETCH (bytes_fetched++, op=*cur_insn++)
#define _FETCH2 { bytes_fetched += 2; op = *cur_insn++; op += (int)(*cur_insn++) << 8; }
    
  Lisp_Object *vectorp;
  vectorp = XVECTOR (vector)->contents;

  int i;
  /* We need to define a label for every instruction in case a goto
     jumps to it; we might be able to scan the bytecode first to find
     the actual number of instructions and find which address gotos go
     to and such, but this will work for now. */
  jit_label_t labels[bytestr_length];
  int num_labels = bytestr_length;
  for(i = 0; i < num_labels; i++) {
    labels[i] = jit_label_undefined;
  }
  
  jit_context_build_start(bytecode_jit_context);

#ifdef JIT_USE_FASTCALL
  #define JIT_ABI jit_abi_fastcall
#else  
  #define JIT_ABI jit_abi_cdecl
#endif

  jit_function_t function = jit_function_create(bytecode_jit_context,
                                                bytecode_jitcall_signature);

  jit_function_set_optimization_level(function, jit_function_get_max_optimization_level());
  jit_value_t _core;
  _core = jit_value_get_param(function, 0);
  jit_value_t op_args[3];
  op_args[0] = _core;

  jit_value_t zero_value =
    jit_value_create_nint_constant(function, jit_type_int, 0);

  int cur_addr;

#ifdef JIT_DEBUG
#define JIT_PRINT_OP_ADDR                                                       \
  fprintf(stderr, "%s op: %d\n", #fun, op);                                     \
  op_args[0] =                                                                  \
    jit_value_create_nint_constant(function, jit_type_nint, (int)stderr);       \
  op_args[1] =                                                                  \
    jit_value_create_nint_constant(function, jit_type_nint, (int)"%d: ");       \
  op_args[2] =                                                                  \
    jit_value_create_nint_constant(function, jit_type_nint, cur_addr);          \
  jit_insn_call_native(function, "fprintf", fprintf, fprintf_sig,               \
    op_args, 3, 0);                                                             \
  op_args[0] = _core;
#else
#define JIT_PRINT_OP_ADDR
#endif

#define JIT_COMPILE_CORE(fun, EXTRA)                                    \
  {                                                                     \
    JIT_PRINT_OP_ADDR;                                                  \
    jit_insn_call_native(function, "bytecode_jit_" # fun,               \
                         bytecode_jit_##fun,                            \
                         bytecode_jit_core_signature, op_args, 1,       \
                         JIT_CALL_NOTHROW);                             \
    EXTRA;                                                              \
    break;                                                              \
  }
  
#define JIT_COMPILE_OP(fun, EXTRA)                                              \
      {                                                                         \
        JIT_PRINT_OP_ADDR;                                                      \
        op_args[1] =                                                            \
           jit_value_create_nint_constant(function, jit_type_nint, op);         \
        jit_insn_call_native(function, "bytecode_jit_" # fun,                   \
                             bytecode_jit_##fun,                                \
                             bytecode_jit_op_signature, op_args, 2,             \
                             JIT_CALL_NOTHROW);                                 \
        EXTRA;                                                                  \
        break;                                                                  \
      }

#define JIT_COMPILE_VECTOR_REF(fun, EXTRA)                                      \
  {                                                                             \
      JIT_PRINT_OP_ADDR;                                                        \
      op_args[1] =                                                              \
        jit_value_create_nint_constant(function, jit_type_void_ptr,             \
      vectorp[op]);                                                             \
      jit_insn_call_native(function, "bytecode_jit_" # fun,                     \
                           bytecode_jit_##fun,                                  \
                           bytecode_jit_vector_ref_signature, op_args, 2,       \
                           JIT_CALL_NOTHROW);                                   \
      EXTRA;                                                                    \
      break;                                                                    \
  }

#define JIT_COMPILE_GOTO(fun)                                           \
  {                                                                     \
   JIT_PRINT_OP_ADDR;                                                   \
   _FETCH2;                                                             \
   CHECK_RANGE (op);                                                    \
   /*fprintf(stderr, "%s op: %d\n", #fun, op);*/                        \
   jit_value_t ret =                                                    \
     jit_insn_call_native(function, "bytecode_jit_" # fun,              \
                            bytecode_jit_##fun,                         \
                            bytecode_jit_goto_signature, op_args, 1,    \
                            JIT_CALL_NOTHROW);                          \
   jit_value_t eq_zero  = jit_insn_eq(function, ret, zero_value);       \
   jit_insn_branch_if_not(function, eq_zero, &labels[op]);              \
   break;                                                               \
   }

  //fprintf(stderr, "bytestr_length: %d\n", bytestr_length);
  while(bytes_fetched < bytestr_length) {
    cur_addr = bytes_fetched;
    /* Define a label at the start of the current instruction */
    jit_insn_label(function, &labels[bytes_fetched]);
    //fprintf(stderr, "%d: ", bytes_fetched);

    _FETCH;

    switch(op) {
    case (Bgoto):
      _FETCH2;
      JIT_COMPILE_CORE(goto, jit_insn_branch(function, &labels[op]));

    case (Bgotoifnil):
      JIT_COMPILE_GOTO(gotoifnil);

    case (Bgotoifnonnil):
      JIT_COMPILE_GOTO(gotoifnonnil);

    case (Bgotoifnilelsepop):
      JIT_COMPILE_GOTO(gotoifnilelsepop);

    case (Bgotoifnonnilelsepop):
      JIT_COMPILE_GOTO(gotoifnonnilelsepop);

      // The following Rgoto's don't even look to be used by the byte compiler
      // so we'll forget about them for now because they are very confusing
      // TODO: ask about them on emacs-devel
    case (BRgoto):
      emacs_abort();
      JIT_COMPILE_CORE(Rgoto, jit_insn_branch(function,
                                         &labels[cur_insn-bytecode_start-127]));

    case (BRgotoifnil):
      emacs_abort();
      JIT_COMPILE_GOTO(Rgotoifnil);

    case (BRgotoifnonnil):
      emacs_abort();
      JIT_COMPILE_GOTO(Rgotoifnonnil);

    case (BRgotoifnilelsepop):
      emacs_abort();
      JIT_COMPILE_GOTO(Rgotoifnilelsepop);

    case (BRgotoifnonnilelsepop):
      emacs_abort();
      JIT_COMPILE_GOTO(Rgotoifnonnilelsepop);

    case (Bvarref7):
      _FETCH2;
      goto varref;
    case (Bvarref):
    case (Bvarref1):
    case (Bvarref2):
    case (Bvarref3):
    case (Bvarref4):
    case (Bvarref5):
      op = op - Bvarref;
      goto varref;
    case (Bvarref6):
      _FETCH;
    varref:
      JIT_COMPILE_VECTOR_REF(varref,);
      
    case (Bcar):
      JIT_COMPILE_CORE(car,);

    case (Beq):
      JIT_COMPILE_CORE(eq,);

    case (Bmemq):
      JIT_COMPILE_CORE(memq,);

    case (Bcdr):
      JIT_COMPILE_CORE(cdr,);

    case (Bvarset):
    case (Bvarset1):
    case (Bvarset2):
    case (Bvarset3):
    case (Bvarset4):
    case (Bvarset5):
      op -= Bvarset;
      goto varset;

    case (Bvarset7):
      _FETCH2;
      goto varset;

    case (Bvarset6):
      _FETCH;
    varset:
      JIT_COMPILE_VECTOR_REF(varset,);

    case (Bdup):
      JIT_COMPILE_CORE(dup,);
      
      /* ------------------ */

    case (Bvarbind6):
      _FETCH;
      goto varbind;

    case (Bvarbind7):
      _FETCH2;
      goto varbind;

    case (Bvarbind):
    case (Bvarbind1):
    case (Bvarbind2):
    case (Bvarbind3):
    case (Bvarbind4):
    case (Bvarbind5):
      op -= Bvarbind;
    varbind:
      JIT_COMPILE_VECTOR_REF(varbind,);

    case (Bcall6):
      _FETCH;
      goto docall;

    case (Bcall7):
      _FETCH2;
      goto docall;

    case (Bcall):
    case (Bcall1):
    case (Bcall2):
    case (Bcall3):
    case (Bcall4):
    case (Bcall5):
      op -= Bcall;
    docall:
      JIT_COMPILE_OP(call,);

    case (Bunbind6):
      _FETCH;
      goto dounbind;

    case (Bunbind7):
      _FETCH2;
      goto dounbind;

    case (Bunbind):
    case (Bunbind1):
    case (Bunbind2):
    case (Bunbind3):
    case (Bunbind4):
    case (Bunbind5):
      op -= Bunbind;
    dounbind:
      JIT_COMPILE_OP(unbind,);

    case (Bunbind_all):	/* Obsolete.  Never used.  */
      JIT_COMPILE_CORE(unbind_all,);

    case (Breturn):
      JIT_COMPILE_CORE(return, jit_insn_return(function, NULL));
      
    case (Bdiscard):
      JIT_COMPILE_CORE(discard,);

    case (Bconstant2):
      _FETCH2;
      JIT_COMPILE_VECTOR_REF(constant2,);
      
    case (Bsave_excursion):
        JIT_COMPILE_CORE(save_excursion,);

    case (Bsave_current_buffer): /* Obsolete since ??.  */
    case (Bsave_current_buffer_1):
      JIT_COMPILE_CORE(save_current_buffer,);

    case (Bsave_window_excursion): /* Obsolete since 24.1.  */
      JIT_COMPILE_CORE(save_window_excursion,);

    case (Bsave_restriction):
      JIT_COMPILE_CORE(save_restriction,);

    case (Bcatch):		/* FIXME: ill-suited for lexbind.  */
      JIT_COMPILE_CORE(catch,);

    case (Bunwind_protect):	/* FIXME: avoid closure for lexbind.  */
      JIT_COMPILE_CORE(unwind_protect,);

    case (Bcondition_case):	/* FIXME: ill-suited for lexbind.  */
      JIT_COMPILE_CORE(condition_case,);

    case (Btemp_output_buffer_setup): /* Obsolete since 24.1.  */
      JIT_COMPILE_CORE(temp_output_buffer_setup,);

    case (Btemp_output_buffer_show): /* Obsolete since 24.1.  */
      JIT_COMPILE_CORE(temp_output_buffer_show,);

    case (Bnth):
      JIT_COMPILE_CORE(nth,);

    case (Bsymbolp):
      JIT_COMPILE_CORE(symbolp,);

    case (Bconsp):
      JIT_COMPILE_CORE(consp,);    

    case (Bstringp):
      JIT_COMPILE_CORE(stringp,);

    case (Blistp):
      JIT_COMPILE_CORE(listp,);
      
    case (Bnot):
      JIT_COMPILE_CORE(not,);

    case (Bcons):
      JIT_COMPILE_CORE(cons,);

    case (Blist1):
      JIT_COMPILE_CORE(list1,);

    case (Blist2):
      JIT_COMPILE_CORE(list2,);

    case (Blist3):
      JIT_COMPILE_CORE(list3,);

    case (Blist4):
      JIT_COMPILE_CORE(list4,);

    case (BlistN):
      _FETCH;
      JIT_COMPILE_OP(listN,);

    case (Blength):
      JIT_COMPILE_CORE(length,);
      
    case (Baref):
      JIT_COMPILE_CORE(aref,);

    case (Baset):
      JIT_COMPILE_CORE(aset,);

    case (Bsymbol_value):
      JIT_COMPILE_CORE(symbol_value,);

    case (Bsymbol_function):
      JIT_COMPILE_CORE(symbol_function,);

    case (Bset):
      JIT_COMPILE_CORE(set,);

    case (Bfset):
      JIT_COMPILE_CORE(fset,);

    case (Bget):
      JIT_COMPILE_CORE(get,);

    case (Bsubstring):
      JIT_COMPILE_CORE(substring,);

    case (Bconcat2):
      JIT_COMPILE_CORE(concat2,);
      
    case (Bconcat3):
      JIT_COMPILE_CORE(concat3,);

    case (Bconcat4):
      JIT_COMPILE_CORE(concat4,);

    case (BconcatN):
      _FETCH;
      JIT_COMPILE_OP(concatN,);

    case (Bsub1):
      JIT_COMPILE_CORE(sub1,);

    case (Badd1):
      JIT_COMPILE_CORE(add1,);

    case (Beqlsign):
      JIT_COMPILE_CORE(eqlsign,);

    case (Bgtr):
      JIT_COMPILE_CORE(gtr,);

    case (Blss):
      JIT_COMPILE_CORE(lss,);

    case (Bleq):
      JIT_COMPILE_CORE(leq,);

    case (Bgeq):
      JIT_COMPILE_CORE(geq,);

    case (Bdiff):
      JIT_COMPILE_CORE(diff,);

    case (Bnegate):
      JIT_COMPILE_CORE(negate,);

    case (Bplus):
      JIT_COMPILE_CORE(plus,);

    case (Bmax):
      JIT_COMPILE_CORE(max,);

    case (Bmin):
      JIT_COMPILE_CORE(min,);

    case (Bmult):
      JIT_COMPILE_CORE(mult,);

    case (Bquo):
      JIT_COMPILE_CORE(quo,);

    case (Brem):
      JIT_COMPILE_CORE(rem,);

    case (Bpoint):
      JIT_COMPILE_CORE(point,);
      
    case (Bgoto_char):
      JIT_COMPILE_CORE(goto_char,);
      
    case (Binsert):
      JIT_COMPILE_CORE(insert,);

    case (BinsertN):
      _FETCH;
      JIT_COMPILE_OP(insertN,);

    case (Bpoint_max):
      JIT_COMPILE_CORE(point_max,);

    case (Bpoint_min):
      JIT_COMPILE_CORE(point_min,);

    case (Bchar_after):
      JIT_COMPILE_CORE(char_after,);

    case (Bfollowing_char):
      JIT_COMPILE_CORE(following_char,);

    case (Bpreceding_char):
      JIT_COMPILE_CORE(preceding_char,);

    case (Bcurrent_column):
      JIT_COMPILE_CORE(current_column,);

    case (Bindent_to):
      JIT_COMPILE_CORE(indent_to,);

    case (Beolp):
      JIT_COMPILE_CORE(eolp,);

    case (Beobp):
      JIT_COMPILE_CORE(eobp,);

    case (Bbolp):
      JIT_COMPILE_CORE(bolp,);

    case (Bbobp):
      JIT_COMPILE_CORE(bobp,);

    case (Bcurrent_buffer):
      JIT_COMPILE_CORE(current_buffer,);

    case (Bset_buffer):
      JIT_COMPILE_CORE(set_buffer,);

    case (Binteractive_p):	/* Obsolete since 24.1.  */
      JIT_COMPILE_CORE(interactive_p,);

    case (Bforward_char):
      JIT_COMPILE_CORE(forward_char,);

    case (Bforward_word):
      JIT_COMPILE_CORE(forward_word,);

    case (Bskip_chars_forward):
      JIT_COMPILE_CORE(skip_chars_forward,);

    case (Bskip_chars_backward):
      JIT_COMPILE_CORE(skip_chars_backward,);

    case (Bforward_line):
      JIT_COMPILE_CORE(forward_line,);

    case (Bchar_syntax):
      JIT_COMPILE_CORE(char_syntax,);

    case (Bbuffer_substring):
      JIT_COMPILE_CORE(buffer_substring,);

    case (Bdelete_region):
      JIT_COMPILE_CORE(delete_region,);

    case (Bnarrow_to_region):
      JIT_COMPILE_CORE(narrow_to_region,);

    case (Bwiden):
      JIT_COMPILE_CORE(widen,);

    case (Bend_of_line):
      JIT_COMPILE_CORE(end_of_line,);

    case (Bset_marker):
      JIT_COMPILE_CORE(set_marker,);

    case (Bmatch_beginning):
      JIT_COMPILE_CORE(match_beginning,);

    case (Bmatch_end):
      JIT_COMPILE_CORE(match_end,);

    case (Bupcase):
      JIT_COMPILE_CORE(upcase,);

    case (Bdowncase):
      JIT_COMPILE_CORE(downcase,);

    case (Bstringeqlsign):
      JIT_COMPILE_CORE(stringeqlsign,);

    case (Bstringlss):
      JIT_COMPILE_CORE(stringlss,);

    case (Bequal):
      JIT_COMPILE_CORE(equal,);

    case (Bnthcdr):
      JIT_COMPILE_CORE(nthcdr,);

    case (Belt):
      JIT_COMPILE_CORE(elt,);

    case (Bmember):
      JIT_COMPILE_CORE(member,);

    case (Bassq):
      JIT_COMPILE_CORE(assq,);

    case (Bnreverse):
      JIT_COMPILE_CORE(nreverse,);

    case (Bsetcar):
      JIT_COMPILE_CORE(setcar,);

    case (Bsetcdr):
      JIT_COMPILE_CORE(setcdr,);

    case (Bcar_safe):
      JIT_COMPILE_CORE(car_safe,);

    case (Bcdr_safe):
      JIT_COMPILE_CORE(cdr_safe,);

    case (Bnconc):
      JIT_COMPILE_CORE(nconc,);

    case (Bnumberp):
      JIT_COMPILE_CORE(numberp,);

    case (Bintegerp):
      JIT_COMPILE_CORE(integerp,);

#ifdef BYTE_CODE_SAFE
	  /* These are intentionally written using 'case' syntax,
	     because they are incompatible with the threaded
	     interpreter.  */

	case Bset_mark:
	  BEFORE_POTENTIAL_GC ();
	  error ("set-mark is an obsolete bytecode");
	  AFTER_POTENTIAL_GC ();
	  break;
	case Bscan_buffer:
	  BEFORE_POTENTIAL_GC ();
	  error ("scan-buffer is an obsolete bytecode");
	  AFTER_POTENTIAL_GC ();
	  break;
#endif

    case_ABORT:
      /* Actually this is Bstack_ref with offset 0, but we use Bdup
         for that instead.  */
      /* case (Bstack_ref): */
      error ("Invalid byte opcode");

      /* Handy byte-codes for lexical binding.  */
      /* Modified to be more like above */
    case (Bstack_ref):
    case (Bstack_ref1):
    case (Bstack_ref2):
    case (Bstack_ref3):
    case (Bstack_ref4):
    case (Bstack_ref5):
      op = op - Bstack_ref;
      goto stack_ref;
    case (Bstack_ref7):
      _FETCH2;
      goto stack_ref;
    case (Bstack_ref6):
      _FETCH;
    stack_ref:
      JIT_COMPILE_OP(stack_ref,);
          
    case (Bstack_set):
      _FETCH;
      JIT_COMPILE_OP(stack_set,);
          
    case (Bstack_set2):
      _FETCH2;
      JIT_COMPILE_OP(stack_set2,);
          
    case (BdiscardN):
      _FETCH;
      JIT_COMPILE_OP(discardN,);

    case(Bjitcall):
      /* Already JIT compiled */
      return Qt;
          
    default:
    case (Bconstant):
#ifdef BYTE_CODE_SAFE
      if (op < Bconstant)
        {
          emacs_abort ();
        }
      if ((op -= Bconstant) >= const_length)
        {
          emacs_abort ();
        }
#else
      op -= Bconstant;
#endif
      JIT_COMPILE_VECTOR_REF(constant,);
    }
  }

  jit_function_compile(function);
  
  jit_context_build_end(bytecode_jit_context);

  Lisp_Object args[1];
  args[0] = build_string("jit-compile: Function has been JIT compiled!");
  Fmessage (1, args);
 
  /* And now the magic; create a new byte code vector that contains a
     single instruction Bjitcall with the jit_function_t as it's
     argument and set it on the passed function argument */
  ptrdiff_t new_bytestr_len = 1 + sizeof(function);
  unsigned char tmp_bytestr[new_bytestr_len];
  tmp_bytestr[0] = Bjitcall;
  memcpy(tmp_bytestr+1, &function, sizeof(function));
      //fprintf(stderr, "function: 0x%x\n", function);
  Lisp_Object new_bytestr = make_string(tmp_bytestr, new_bytestr_len);
  ASET (fun, COMPILED_BYTECODE, new_bytestr);
  
  return Qt;
}

Lisp_Object Qjit_compile;

void
bytecode_jit_init(void)
{
  //fprintf(stderr, "bytecode_jit_init()\n");
  bytecode_jit_context = jit_context_create();
  
  jit_type_t params[3];
  
  params[0] = jit_type_void_ptr;
  bytecode_jitcall_signature = jit_type_create_signature
    (jit_abi_cdecl, jit_type_void, params, 1, 1);
  
  bytecode_jit_core_signature = jit_type_create_signature
    (JIT_ABI, jit_type_void, params, 1, 1);
  
  bytecode_jit_goto_signature = jit_type_create_signature
    (JIT_ABI, jit_type_int, params, 1, 1);
  
  params[1] = jit_type_int;
  bytecode_jit_op_signature = jit_type_create_signature
    (JIT_ABI, jit_type_void, params, 2, 1);
  
  params[1] = jit_type_void_ptr;
  bytecode_jit_vector_ref_signature = jit_type_create_signature
    (JIT_ABI, jit_type_void, params, 2, 1);
  
#ifdef JIT_DEBUG  
  params[0] = jit_type_void_ptr;
  params[1] = jit_type_void_ptr;
  params[2] = jit_type_int;
  fprintf_sig = jit_type_create_signature
    (jit_abi_vararg, jit_type_int, params, 3, 1);
#endif
}

DEFUN ("jit-compile", Fjit_compile, Sjit_compile, 1, 1, 0,
       doc: /* JIT compile the bytecode of the function FUN. */)
     (Lisp_Object fun)
{
  /* FIXME: This should be moved somewhere else */
  if(!bytecode_jit_context) {
    bytecode_jit_init();
  }

  /* If the function is not byte-compiled, byte compile it */
  if(!COMPILEDP(fun)) {
    call1(intern("byte-compile"), fun);
  }

  /* If we were passed a symbol, get it's symbol-function */;
  if(SYMBOLP(fun)) {
    #if 0
    Lisp_Object args[2];
    args[0] = build_string("jit-compile: JIT compiling %s.");
    args[1] = fun;
    Fmessage (2, args);
    #endif
    fun = Fsymbol_function(fun);
  }

  return jit_bytecode_compile(fun);
}

void jit_hotspot_bytecode(Lisp_Object fun) {
  /* FIXME: This should be moved somewhere else */
  if(!bytecode_jit_context) {
    bytecode_jit_init();
  }
  
  Fjit_compile(fun);
}

void
syms_of_jit_compiler (void)
{
  DEFSYM (Qjit_compile, "jit-compile");

  defsubr (&Sjit_compile);
}
