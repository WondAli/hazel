open Sexplib.Std /* hole instance numbers are all 0 after expansion and during evaluation -- * renumbering is done on the final result (see below) */;

[@deriving sexp]
type result =
  | InvalidInput(int)
  | BoxedValue(DHExp.t)
  | Indet(DHExp.t);

/*
   0 = out of fuel
   1 = free or invalid variable
   2 = ap invalid boxed function val
   3 = boxed value not a int literal 2
   4 = boxed value not a int literal 1
   5 = bad pattern match
   6 = Cast BV Hole Ground
   7 = boxed value not a float literal 1
   8 = boxed value not a float literal 2
   9 = boxed value not a string literal
 */

[@deriving sexp]
type ground_cases =
  | Hole
  | Ground
  | NotGroundOrHole(HTyp.t); /* the argument is the corresponding ground type */

let grounded_Arrow = NotGroundOrHole(Arrow(Hole, Hole));
let grounded_Sum = NotGroundOrHole(Sum(Hole, Hole));
let grounded_Prod = length =>
  NotGroundOrHole(Prod(ListUtil.replicate(length, HTyp.Hole)));
let grounded_List = NotGroundOrHole(List(Hole));

let ground_cases_of = (ty: HTyp.t): ground_cases =>
  switch (ty) {
  | Hole => Hole
  | Bool
  | String
  | Int
  | Float
  | Arrow(Hole, Hole)
  | Sum(Hole, Hole)
  | List(Hole) => Ground
  | Prod(tys) =>
    if (List.for_all(HTyp.eq(HTyp.Hole), tys)) {
      Ground;
    } else {
      tys |> List.length |> grounded_Prod;
    }
  | Arrow(_, _) => grounded_Arrow
  | Sum(_, _) => grounded_Sum
  | List(_) => grounded_List
  };

let eval_bin_bool_op = (op: DHExp.BinBoolOp.t, b1: bool, b2: bool): DHExp.t =>
  switch (op) {
  | And => BoolLit(b1 && b2)
  | Or => BoolLit(b1 || b2)
  };

let eval_bin_int_op = (op: DHExp.BinIntOp.t, n1: int, n2: int): DHExp.t => {
  switch (op) {
  | Minus => IntLit(n1 - n2)
  | Plus => IntLit(n1 + n2)
  | Times => IntLit(n1 * n2)
  | Divide => IntLit(n1 / n2)
  | LessThan => BoolLit(n1 < n2)
  | GreaterThan => BoolLit(n1 > n2)
  | Equals => BoolLit(n1 == n2)
  };
};

let eval_bin_float_op =
    (op: DHExp.BinFloatOp.t, f1: float, f2: float): DHExp.t => {
  switch (op) {
  | FPlus => FloatLit(f1 +. f2)
  | FMinus => FloatLit(f1 -. f2)
  | FTimes => FloatLit(f1 *. f2)
  | FDivide => FloatLit(f1 /. f2)
  | FLessThan => BoolLit(f1 < f2)
  | FGreaterThan => BoolLit(f1 > f2)
  | FEquals => BoolLit(f1 == f2)
  };
};

let eval_bin_str_op = (op: DHExp.BinStrOp.t, n1: string, n2: string): DHExp.t =>
  switch (op) {
  | Caret => StringLit(n1 ++ n2)
  };

let builtinfunctions_evaluate = (x: string, d: DHExp.t): result =>
  switch (d) {
  | StringLit(s) =>
    switch (x) {
    | "length" => BoxedValue(IntLit(String.length(s)))
    | "int_of_string" =>
      if (BuiltinFunctions.is_int_of_string(s)
          && float_of_string(s) <= 2147483647.) {
        BoxedValue(IntLit(int_of_string(s)));
      } else {
        Indet(InvalidOperation(Ap(BuiltInLit(x), d), StrNotConvToInt));
      }
    | "bool_of_string" =>
      if (BuiltinFunctions.is_bool_of_string(s)) {
        BoxedValue(BoolLit(bool_of_string(s)));
      } else {
        Indet(InvalidOperation(Ap(BuiltInLit(x), d), StrNotConvToBool));
      }
    | "float_of_string" =>
      if (BuiltinFunctions.is_float_of_string(s)) {
        BoxedValue(FloatLit(float_of_string(s)));
      } else {
        Indet(InvalidOperation(Ap(BuiltInLit(x), d), StrNotConvToFloat));
      }
    | "trim" =>
      print_endline("TRIM s=" ++ s);
      let (s', _) = StringUtil.find_and_replace("", s, "OK");
      BoxedValue(StringLit(String.trim(s')));
    | "escaped" =>
      print_endline("ESCAPED s=" ++ s);
      BoxedValue(StringLit(String.escaped(s)));
    /* multiple arguments */
    | "equal" =>
      BoxedValue(Lam(Var(x), String, BoolLit(String.equal(s, x))))
    | "compare" =>
      BoxedValue(Lam(Var(x), String, IntLit(String.compare(s, x))))
    | _ => Indet(Ap(BuiltInLit(x), d))
    }
  | IntLit(n) =>
    switch (x) {
    | "string_of_int" => BoxedValue(StringLit(string_of_int(n)))
    | "float_of_int" => BoxedValue(FloatLit(float_of_int(n)))
    | _ => Indet(Ap(BuiltInLit(x), d))
    }
  | BoolLit(b) =>
    switch (x) {
    | "string_of_bool" => BoxedValue(StringLit(string_of_bool(b)))
    | "assert" =>
      b ? Indet(FailedAssert(d)) : BoxedValue(StringLit("assertion"))
    | _ => Indet(Ap(BuiltInLit(x), d))
    }
  | FloatLit(n) =>
    switch (x) {
    | "string_of_float" => BoxedValue(StringLit(string_of_float(n)))
    | "int_of_float" => BoxedValue(IntLit(int_of_float(n)))
    | _ => Indet(Ap(BuiltInLit(x), d))
    }
  | _ => Indet(Ap(BuiltInLit(x), d))
  };

let rec evaluate = (d: DHExp.t): result =>
  switch (d) {
  | BoundVar(_) => InvalidInput(1)
  | FailedAssert(d1) => Indet(d1)
  | Let(dp, d1, d2) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(d1)
    | Indet(d1) =>
      switch (Elaborator.Exp.matches(dp, d1)) {
      | Indet => Indet(d)
      | DoesNotMatch => Indet(d)
      | Matches(env) => evaluate(Elaborator.Exp.subst(env, d2))
      }
    }
  | FixF(x, _, d1) => evaluate(Elaborator.Exp.subst_var(d, x, d1))
  | Lam(_, _, _) => BoxedValue(d)
  | Ap(d1, d2) =>
    print_endline("EVALUATE AP Dynamics2202");
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(BuiltInLit(v)) =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2') =>
        print_endline("Eva174");
        builtinfunctions_evaluate(v, d2');
      | Indet(d2) =>
        print_endline("Eva177");
        switch (d2) {
        /* int overflow */
        | Cast(NonEmptyHole(_, _, _, _, FloatLit(n)), _, Int)
            when Float.is_integer(n) =>
          Indet(InvalidOperation(Ap(d1, d2), IntOutBound))
        | _ => Indet(Ap(d1, d2))
        };
      }
    | BoxedValue(Lam(dp, _, d3)) =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2)
      | Indet(d2) =>
        switch (Elaborator.Exp.matches(dp, d2)) {
        | DoesNotMatch => Indet(d)
        | Indet => Indet(d)
        | Matches(env) =>
          /* beta rule */
          evaluate(Elaborator.Exp.subst(env, d3))
        }
      }
    | BoxedValue(Cast(d1', Arrow(ty1, ty2), Arrow(ty1', ty2')))
    | Indet(Cast(d1', Arrow(ty1, ty2), Arrow(ty1', ty2'))) =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2')
      | Indet(d2') =>
        /* ap cast rule */
        evaluate(Cast(Ap(d1', Cast(d2', ty1', ty1)), ty2, ty2'))
      }
    | BoxedValue(_) => InvalidInput(2)
    | Indet(d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2')
      | Indet(d2') => Indet(Ap(d1', d2'))
      }
    };
  | Subscript(d1, d2, d3) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(StringLit(s1) as s1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(IntLit(n1) as n1') =>
        switch (evaluate(d3)) {
        | InvalidInput(msg) => InvalidInput(msg)
        | BoxedValue(IntLit(n2) as n2') =>
          let len = String.length(s1);
          if (n1 >= (-1) * len && n1 <= len && n2 >= (-1) * len && n2 <= len) {
            let n1' =
              if (n1 < 0) {
                n1 + len;
              } else {
                n1;
              };
            let n2' =
              if (n2 < 0) {
                n2 + len;
              } else {
                n2;
              };
            BoxedValue(StringLit(String.sub(s1, n1', max(0, n2' - n1'))));
          } else {
            Indet(
              InvalidOperation(Subscript(s1', n1', n2'), IndexOutBound),
            );
          };
        | BoxedValue(_) => InvalidInput(3)
        | Indet(n2') => Indet(Subscript(s1', n1', n2'))
        }
      | BoxedValue(_) => InvalidInput(4)
      | Indet(n1') =>
        switch (evaluate(d3)) {
        | InvalidInput(msg) => InvalidInput(msg)
        | BoxedValue(n2')
        | Indet(n2') => Indet(Subscript(s1', n1', n2'))
        }
      }
    | BoxedValue(s1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(n1')
      | Indet(n1') =>
        switch (evaluate(d3)) {
        | InvalidInput(msg) => InvalidInput(msg)
        | BoxedValue(n2')
        | Indet(n2') => Indet(Subscript(s1', n1', n2'))
        }
      }

    | Indet(s1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(n1')
      | Indet(n1') =>
        switch (evaluate(d3)) {
        | InvalidInput(msg) => InvalidInput(msg)
        | BoxedValue(n2')
        | Indet(n2') => Indet(Subscript(s1', n1', n2'))
        }
      }
    }
  | ListNil(_)
  | BoolLit(_)
  | IntLit(_)
  | FloatLit(_)
  | BuiltInLit(_)
  | Triv => BoxedValue(d)
  | StringLit(s) =>
    let (_, err) = StringUtil.find_and_replace("", s, "OK");
    switch (err) {
    | "OK" => BoxedValue(StringLit(s))
    | "Illegal" => Indet(InvalidOperation(StringLit(s), IllegalEscape))
    | _ => Indet(InvalidOperation(StringLit(s), StrNotTerminate))
    };
  | BinBoolOp(op, d1, d2) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(BoolLit(b1) as d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(BoolLit(b2)) => BoxedValue(eval_bin_bool_op(op, b1, b2))
      | BoxedValue(_) => InvalidInput(3)
      | Indet(d2') => Indet(BinBoolOp(op, d1', d2'))
      }
    | BoxedValue(_) => InvalidInput(4)
    | Indet(d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2')
      | Indet(d2') => Indet(BinBoolOp(op, d1', d2'))
      }
    }
  | BinIntOp(op, d1, d2) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(IntLit(n1) as d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(IntLit(n2)) =>
        switch (op, n1, n2) {
        | (Divide, _, 0) =>
          Indet(
            InvalidOperation(
              BinIntOp(op, IntLit(n1), IntLit(n2)),
              DivideByZero,
            ),
          )
        | _ => BoxedValue(eval_bin_int_op(op, n1, n2))
        }
      | BoxedValue(_) => InvalidInput(3)
      | Indet(d2') => Indet(BinIntOp(op, d1', d2'))
      }
    | BoxedValue(_) => InvalidInput(4)
    | Indet(d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2')
      | Indet(d2') => Indet(BinIntOp(op, d1', d2'))
      }
    }
  | BinFloatOp(op, d1, d2) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(FloatLit(f1) as d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(FloatLit(f2)) =>
        BoxedValue(eval_bin_float_op(op, f1, f2))
      | BoxedValue(_) => InvalidInput(8)
      | Indet(d2') => Indet(BinFloatOp(op, d1', d2'))
      }
    | BoxedValue(_) => InvalidInput(7)
    | Indet(d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2')
      | Indet(d2') => Indet(BinFloatOp(op, d1', d2'))
      }
    }
  | BinStrOp(op, d1, d2) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(StringLit(n1) as d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(StringLit(n2)) => BoxedValue(eval_bin_str_op(op, n1, n2))
      | BoxedValue(_) => InvalidInput(3)
      | Indet(d2') => Indet(BinStrOp(op, d1', d2'))
      }
    | BoxedValue(_) => InvalidInput(4)
    | Indet(d1') =>
      switch (evaluate(d2)) {
      | InvalidInput(msg) => InvalidInput(msg)
      | BoxedValue(d2')
      | Indet(d2') => Indet(BinStrOp(op, d1', d2'))
      }
    }
  | Inj(ty, side, d1) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(d1') => BoxedValue(Inj(ty, side, d1'))
    | Indet(d1') => Indet(Inj(ty, side, d1'))
    }
  | Pair(d1, d2) =>
    switch (evaluate(d1), evaluate(d2)) {
    | (InvalidInput(msg), _)
    | (_, InvalidInput(msg)) => InvalidInput(msg)
    | (Indet(d1), Indet(d2))
    | (Indet(d1), BoxedValue(d2))
    | (BoxedValue(d1), Indet(d2)) => Indet(Pair(d1, d2))
    | (BoxedValue(d1), BoxedValue(d2)) => BoxedValue(Pair(d1, d2))
    }
  | Cons(d1, d2) =>
    switch (evaluate(d1), evaluate(d2)) {
    | (InvalidInput(msg), _)
    | (_, InvalidInput(msg)) => InvalidInput(msg)
    | (Indet(d1), Indet(d2))
    | (Indet(d1), BoxedValue(d2))
    | (BoxedValue(d1), Indet(d2)) => Indet(Cons(d1, d2))
    | (BoxedValue(d1), BoxedValue(d2)) => BoxedValue(Cons(d1, d2))
    }
  | ConsistentCase(Case(d1, rules, n)) => evaluate_case(None, d1, rules, n)
  | InconsistentBranches(u, i, sigma, Case(d1, rules, n)) =>
    evaluate_case(Some((u, i, sigma)), d1, rules, n)
  | EmptyHole(_) => Indet(d)
  | NonEmptyHole(reason, u, i, sigma, d1) =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(d1')
    | Indet(d1') => Indet(NonEmptyHole(reason, u, i, sigma, d1'))
    }
  | FreeVar(_) => Indet(d)
  | Keyword(_) => Indet(d)
  | Cast(d1, ty, ty') =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(d1') as result =>
      switch (ground_cases_of(ty), ground_cases_of(ty')) {
      | (Hole, Hole) => result
      | (Ground, Ground) =>
        /* if two types are ground and consistent, then they are eq */
        result
      | (Ground, Hole) =>
        /* can't remove the cast or do anything else here, so we're done */
        BoxedValue(Cast(d1', ty, ty'))
      | (Hole, Ground) =>
        /* by canonical forms, d1' must be of the form d<ty'' -> ?> */
        switch (d1') {
        | Cast(d1'', ty'', Hole) =>
          if (HTyp.eq(ty'', ty')) {
            BoxedValue(d1'');
          } else {
            Indet(FailedCast(d1', ty, ty'));
          }
        | _ =>
          // TODO: can we omit this? or maybe call logging? JSUtil.log(DHExp.constructor_string(d1'));
          InvalidInput(6)
        }
      | (Hole, NotGroundOrHole(ty'_grounded)) =>
        /* ITExpand rule */
        let d' = DHExp.Cast(Cast(d1', ty, ty'_grounded), ty'_grounded, ty');
        evaluate(d');
      | (NotGroundOrHole(ty_grounded), Hole) =>
        /* ITGround rule */
        let d' = DHExp.Cast(Cast(d1', ty, ty_grounded), ty_grounded, ty');
        evaluate(d');
      | (Ground, NotGroundOrHole(_))
      | (NotGroundOrHole(_), Ground) =>
        /* can't do anything when casting between diseq, non-hole types */
        BoxedValue(Cast(d1', ty, ty'))
      | (NotGroundOrHole(_), NotGroundOrHole(_)) =>
        /* they might be eq in this case, so remove cast if so */
        if (HTyp.eq(ty, ty')) {
          result;
        } else {
          BoxedValue(Cast(d1', ty, ty'));
        }
      }
    | Indet(d1') as result =>
      switch (ground_cases_of(ty), ground_cases_of(ty')) {
      | (Hole, Hole) => result
      | (Ground, Ground) =>
        /* if two types are ground and consistent, then they are eq */
        result
      | (Ground, Hole) =>
        /* can't remove the cast or do anything else here, so we're done */
        Indet(Cast(d1', ty, ty'))
      | (Hole, Ground) =>
        switch (d1') {
        | Cast(d1'', ty'', Hole) =>
          if (HTyp.eq(ty'', ty')) {
            Indet(d1'');
          } else {
            Indet(FailedCast(d1', ty, ty'));
          }
        | _ => Indet(Cast(d1', ty, ty'))
        }
      | (Hole, NotGroundOrHole(ty'_grounded)) =>
        /* ITExpand rule */
        let d' = DHExp.Cast(Cast(d1', ty, ty'_grounded), ty'_grounded, ty');
        evaluate(d');
      | (NotGroundOrHole(ty_grounded), Hole) =>
        /* ITGround rule */
        let d' = DHExp.Cast(Cast(d1', ty, ty_grounded), ty_grounded, ty');
        evaluate(d');
      | (Ground, NotGroundOrHole(_))
      | (NotGroundOrHole(_), Ground) =>
        /* can't do anything when casting between diseq, non-hole types */
        Indet(Cast(d1', ty, ty'))
      | (NotGroundOrHole(_), NotGroundOrHole(_)) =>
        /* it might be eq in this case, so remove cast if so */
        if (HTyp.eq(ty, ty')) {
          result;
        } else {
          Indet(Cast(d1', ty, ty'));
        }
      }
    }
  | FailedCast(d1, ty, ty') =>
    switch (evaluate(d1)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(d1')
    | Indet(d1') => Indet(FailedCast(d1', ty, ty'))
    }
  | InvalidOperation(d, err) =>
    switch (evaluate(d)) {
    | InvalidInput(msg) => InvalidInput(msg)
    | BoxedValue(d')
    | Indet(d') => Indet(InvalidOperation(d', err))
    }
  }
and evaluate_case =
    (
      inconsistent_info,
      scrut: DHExp.t,
      rules: list(DHExp.rule),
      current_rule_index: int,
    )
    : result =>
  switch (evaluate(scrut)) {
  | InvalidInput(msg) => InvalidInput(msg)
  | BoxedValue(scrut)
  | Indet(scrut) =>
    switch (List.nth_opt(rules, current_rule_index)) {
    | None =>
      let case = DHExp.Case(scrut, rules, current_rule_index);
      switch (inconsistent_info) {
      | None => Indet(ConsistentCase(case))
      | Some((u, i, sigma)) =>
        Indet(InconsistentBranches(u, i, sigma, case))
      };
    | Some(Rule(dp, d)) =>
      switch (Elaborator.Exp.matches(dp, scrut)) {
      | Indet =>
        let case = DHExp.Case(scrut, rules, current_rule_index);
        switch (inconsistent_info) {
        | None => Indet(ConsistentCase(case))
        | Some((u, i, sigma)) =>
          Indet(InconsistentBranches(u, i, sigma, case))
        };
      | Matches(env) => evaluate(Elaborator.Exp.subst(env, d))
      | DoesNotMatch =>
        evaluate_case(inconsistent_info, scrut, rules, current_rule_index + 1)
      }
    }
  };
