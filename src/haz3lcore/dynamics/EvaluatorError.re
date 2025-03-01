open Sexplib.Std;

[@deriving (show({with_path: false}), sexp, yojson)]
type t =
  | OutOfFuel
  | FreeInvalidVar(Var.t)
  | BadPatternMatch
  | CastBVHoleGround(DHExp.t)
  | InvalidBoxedFun(DHExp.t)
  | InvalidBoxedBoolLit(DHExp.t)
  | InvalidBoxedIntLit(DHExp.t)
  | InvalidBoxedFloatLit(DHExp.t)
  | InvalidBoxedListLit(DHExp.t)
  | InvalidBoxedStringLit(DHExp.t)
  | InvalidBoxedTuple(DHExp.t)
  | InvalidBuiltin(string)
  | BadBuiltinAp(string, list(DHExp.t));

exception Exception(t);
