open Virtual_dom.Vdom;
open Node;
open Haz3lcore;
open Util;
open Util.Web;

let of_delim' =
  Core.Memo.general(
    ~cache_size_bound=100000,
    ((sort, is_consistent, is_complete, label, i)) => {
      let cls =
        switch (label) {
        | [_] when !is_consistent => "mono-inconsistent"
        | [s] when Form.is_string(s) => "mono-string-lit"
        | [_] => "mono"
        | _ when !is_consistent => "delim-inconsistent"
        | _ when !is_complete => "delim-incomplete"
        | _ => "delim"
        };
      [
        span(
          ~attr=
            Attr.classes(["token", cls, "text-" ++ Sort.to_string(sort)]),
          [Node.text(List.nth(label, i))],
        ),
      ];
    },
  );
let of_delim =
    (sort: Sort.t, is_consistent, t: Piece.tile, i: int): list(Node.t) =>
  of_delim'((sort, is_consistent, Tile.is_complete(t), t.label, i));

let of_grout = [Node.text(Unicode.nbsp)];

let of_secondary =
  Core.Memo.general(
    ~cache_size_bound=1000000, ((secondary_icons, indent, content)) =>
    if (String.equal(Secondary.get_string(content), Form.linebreak)) {
      let str = secondary_icons ? Form.linebreak : "";
      [
        span_c("linebreak", [text(str)]),
        Node.br(),
        Node.text(StringUtil.repeat(indent, Unicode.nbsp)),
      ];
    } else if (String.equal(Secondary.get_string(content), Form.space)) {
      let str = secondary_icons ? "·" : Unicode.nbsp;
      [span_c("secondary", [text(str)])];
    } else if (Secondary.content_is_comment(content)) {
      [span_c("comment", [Node.text(Secondary.get_string(content))])];
    } else {
      [span_c("secondary", [Node.text(Secondary.get_string(content))])];
    }
  );

module Text = (M: {
                 let map: Measured.t;
                 let settings: ModelSettings.t;
               }) => {
  let m = p => Measured.find_p(p, M.map);
  let rec of_segment =
          (~no_sorts=false, ~sort=Sort.root, seg: Segment.t): list(Node.t) => {
    //note: no_sorts flag is used for backback
    let expected_sorts =
      no_sorts
        ? List.init(List.length(seg), i => (i, Sort.Any))
        : Segment.expected_sorts(sort, seg);
    let sort_of_p_idx = idx =>
      switch (List.assoc_opt(idx, expected_sorts)) {
      | None => Sort.Any
      | Some(sort) => sort
      };
    seg
    |> List.mapi((i, p) => (i, p))
    |> List.concat_map(((i, p)) => of_piece(sort_of_p_idx(i), p));
  }
  and of_piece = (expected_sort: Sort.t, p: Piece.t): list(Node.t) => {
    switch (p) {
    | Tile(t) => of_tile(expected_sort, t)
    | Grout(_) => of_grout
    | Secondary({content, _}) =>
      of_secondary((M.settings.secondary_icons, m(p).last.col, content))
    };
  }
  and of_tile = (expected_sort: Sort.t, t: Tile.t): list(Node.t) => {
    let children_and_sorts =
      List.mapi(
        (i, (l, child, r)) =>
          //TODO(andrew): more subtle logic about sort acceptability
          (child, l + 1 == r ? List.nth(t.mold.in_, i) : Sort.Any),
        Aba.aba_triples(Aba.mk(t.shards, t.children)),
      );
    let is_consistent = Sort.consistent(t.mold.out, expected_sort);
    Aba.mk(t.shards, children_and_sorts)
    |> Aba.join(of_delim(t.mold.out, is_consistent, t), ((seg, sort)) =>
         of_segment(~sort, seg)
       )
    |> List.concat;
  };
};

let rec holes =
        (~font_metrics, ~map: Measured.t, seg: Segment.t): list(Node.t) =>
  seg
  |> List.concat_map(
       fun
       | Piece.Secondary(_) => []
       | Tile(t) => List.concat_map(holes(~map, ~font_metrics), t.children)
       | Grout(g) => [
           EmptyHoleDec.view(
             ~font_metrics, // TODO(d) fix sort
             {
               measurement: Measured.find_g(g, map),
               mold: Mold.of_grout(g, Any),
             },
           ),
         ],
     );

let simple_view = (~unselected, ~map, ~settings: ModelSettings.t): Node.t => {
  module Text =
    Text({
      let map = map;
      let settings = settings;
    });
  div(
    ~attr=Attr.class_("code"),
    [span_c("code-text", Text.of_segment(unselected))],
  );
};

let view =
    (
      ~font_metrics,
      ~segment,
      ~unselected,
      ~measured,
      ~settings: ModelSettings.t,
    )
    : Node.t => {
  module Text =
    Text({
      let map = measured;
      let settings = settings;
    });
  let unselected =
    TimeUtil.measure_time("Code.view/unselected", settings.benchmark, () =>
      Text.of_segment(unselected)
    );
  let holes =
    TimeUtil.measure_time("Code.view/holes", settings.benchmark, () =>
      holes(~map=measured, ~font_metrics, segment)
    );
  div(
    ~attr=Attr.class_("code"),
    [
      span_c("code-text", unselected),
      // TODO restore (already regressed so no loss in commenting atm)
      // span_c("code-text-shards", Text.of_segment(segment)),
      ...holes,
    ],
  );
};
