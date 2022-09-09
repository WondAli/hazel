open Sexplib.Std;
open Util;
open Haz3lcore;

[@deriving (show({with_path: false}), sexp, yojson)]
type settings_action =
  | Captions
  | WhitespaceIcons
  | Statics
  | Dynamics
  | ContextInspector
  | InstructorMode
  | Mode(Editors.mode);

[@deriving (show({with_path: false}), sexp, yojson)]
type t =
  | Set(settings_action)
  | UpdateDoubleTap(option(float))
  | Mousedown
  | Mouseup
  | LoadDefault
  | Save
  | ToggleMode
  | SwitchEditor(int)
  | SetFontMetrics(FontMetrics.t)
  | SetLogoFontMetrics(FontMetrics.t)
  | PerformAction(Action.t)
  | FailedInput(FailedInput.reason) //TODO(andrew): refactor as failure?
  | Copy
  | Paste
  | Undo
  | Redo
  | SetShowBackpackTargets(bool)
  | MoveToNextHole(Direction.t)
  | UpdateResult(ModelResults.Key.t, ModelResult.current);

module Failure = {
  [@deriving (show({with_path: false}), sexp, yojson)]
  type t =
    | CantUndo
    | CantRedo
    | CantPaste
    | FailedToLoad
    | FailedToSwitch
    | UnrecognizedInput(FailedInput.reason)
    | FailedToPerform(Action.Failure.t)
    | Exception(string);
};

module Result = {
  include Result;
  type t('success) = Result.t('success, Failure.t);
};

let save = (model: Model.t): unit =>
  switch (model.editors) {
  | Scratch(n, eds) => LocalStorage.save_scratch((model.id_gen, n, eds))
  | School(state) => LocalStorage.save_school((model.id_gen, state))
  };

let update_settings = (a: settings_action, model: Model.t): Model.t => {
  let settings = model.settings;
  let model =
    switch (a) {
    | Statics =>
      /* NOTE: dynamics depends on statics, so if dynamics is on and
         we're turning statics off, turn dynamics off as well */
      {
        ...model,
        settings: {
          ...settings,
          statics: !settings.statics,
          dynamics: !settings.statics && settings.dynamics,
        },
      }
    | Dynamics => {
        ...model,
        settings: {
          ...settings,
          dynamics: !settings.dynamics,
        },
      }
    | Captions => {
        ...model,
        settings: {
          ...settings,
          captions: !settings.captions,
        },
      }
    | WhitespaceIcons => {
        ...model,
        settings: {
          ...settings,
          whitespace_icons: !settings.whitespace_icons,
        },
      }
    | ContextInspector => {
        ...model,
        settings: {
          ...settings,
          context_inspector: !settings.context_inspector,
        },
      }
    | InstructorMode =>
      let new_mode = !settings.instructor_mode;
      {
        ...model,
        editors: Editors.set_instructor_mode(model.editors, new_mode),
        settings: {
          ...settings,
          instructor_mode: !settings.instructor_mode,
        },
      };
    | Mode(mode) => {
        ...model,
        settings: {
          ...settings,
          mode,
        },
      }
    };
  LocalStorage.save_settings(model.settings);
  model;
};

let load_editor = (model: Model.t): Model.t => {
  let m =
    switch (model.settings.mode) {
    | Scratch =>
      let (id_gen, idx, editors) = LocalStorage.load_scratch();
      {...model, id_gen, editors: Scratch(idx, editors)};
    | School =>
      let instructor_mode = model.settings.instructor_mode;
      let (id_gen, state) = LocalStorage.load_school(~instructor_mode);
      {...model, id_gen, editors: School(state)};
    };
  {
    ...m,
    results:
      ModelResults.init(
        model.settings.dynamics ? Editors.get_spliced_elabs(m.editors) : [],
      ),
  };
};

let load_default_editor = (model: Model.t): Model.t =>
  switch (model.editors) {
  | Scratch(_) =>
    let (id_gen, idx, editors) = Scratch.init;
    {...model, editors: Scratch(idx, editors), id_gen};
  | School(_) =>
    let instructor_mode = model.settings.instructor_mode;
    let (id_gen, state) = School.init(~instructor_mode);
    {...model, editors: School(state), id_gen};
  };

let reevaluate_post_update =
  fun
  | Set(s_action) =>
    switch (s_action) {
    | Captions
    | WhitespaceIcons
    | Statics => false
    | Dynamics
    | InstructorMode
    | ContextInspector
    | Mode(_) => true
    }
  | PerformAction(
      Move(_) | Select(_) | Unselect | RotateBackpack | MoveToBackpackTarget(_),
    )
  | MoveToNextHole(_) //
  | UpdateDoubleTap(_)
  | Mousedown
  | Mouseup
  | Save
  | SetShowBackpackTargets(_)
  | SetFontMetrics(_)
  | SetLogoFontMetrics(_)
  | Copy
  | UpdateResult(_)
  | FailedInput(_) => false
  // may not be necessary on all of these
  // TODO review and prune
  | PerformAction(Destruct(_) | Insert(_) | Pick_up | Put_down)
  | LoadDefault
  | SwitchEditor(_)
  | ToggleMode
  | Paste
  | Undo
  | Redo => true;

let evaluate_and_schedule =
    (state: State.t, ~schedule_action, model: Model.t): Model.t => {
  if (model.settings.dynamics) {
    Editors.get_spliced_elabs(model.editors)
    |> List.iter(((key, d)) => {
         /* Send evaluation request. */
         let pushed = State.evaluator_next(state, key, d);

         /* Set evaluation to pending after short timeout. */
         /* FIXME: This is problematic if evaluation finished in time, but UI hasn't
          * updated before below action is scheduled. */
         Delay.delay(
           () =>
             if (pushed |> Lwt.is_sleeping) {
               schedule_action(UpdateResult(key, ResultPending));
             },
           300,
         );
       });
  };
  model;
};

let apply =
    (model: Model.t, update: t, state: State.t, ~schedule_action)
    : Result.t(Model.t) => {
  let m: Result.t(Model.t) =
    switch (update) {
    | Set(s_action) => Ok(update_settings(s_action, model))
    | UpdateDoubleTap(double_tap) => Ok({...model, double_tap})
    | Mousedown => Ok({...model, mousedown: true})
    | Mouseup => Ok({...model, mousedown: false})
    | LoadDefault => Ok(load_default_editor(model))
    | Save =>
      save(model);
      Ok(model);
    | SwitchEditor(n) =>
      switch (model.editors) {
      | Scratch(m, _) when m == n => Error(FailedToSwitch)
      | Scratch(_, zs) =>
        switch (n < List.length(zs)) {
        | false => Error(FailedToSwitch)
        | true =>
          LocalStorage.save_scratch((model.id_gen, n, zs));
          Ok({...model, editors: Scratch(n, zs)});
        }
      | School(state) =>
        LocalStorage.save_school((model.id_gen, state));
        let state = SchoolExercise.switch_editor(n, state);
        Ok({...model, editors: School(state)});
      }
    | ToggleMode =>
      let new_mode = Editors.rotate_mode(model.editors);
      let model = update_settings(Mode(new_mode), model);
      Ok(load_editor(model));
    | SetShowBackpackTargets(b) => Ok({...model, show_backpack_targets: b})
    | SetFontMetrics(font_metrics) => Ok({...model, font_metrics})
    | SetLogoFontMetrics(logo_font_metrics) =>
      Ok({...model, logo_font_metrics})
    | PerformAction(a) =>
      let ed_init = Editors.get_editor(model.editors);
      switch (Haz3lcore.Perform.go(a, ed_init, model.id_gen)) {
      | Error(err) => Error(FailedToPerform(err))
      | Ok((ed, id_gen)) =>
        Ok({
          ...model,
          id_gen,
          editors: Editors.put_editor(ed, model.editors),
        })
      };
    | FailedInput(reason) => Error(UnrecognizedInput(reason))
    | Copy =>
      let clipboard =
        Printer.to_string_selection(Editors.get_zipper(model.editors));
      //JsUtil.copy_to_clipboard(clipboard);
      Ok({...model, clipboard});
    | Paste =>
      //let clipboard = JsUtil.get_from_clipboard();
      let clipboard = model.clipboard;
      let ed = Editors.get_editor(model.editors);
      switch (
        Printer.zipper_of_string(
          ~zipper_init=ed.state.zipper,
          model.id_gen,
          clipboard,
        )
      ) {
      | None => Error(CantPaste)
      | Some((z, id_gen)) =>
        //TODO: add correct action to history (Pick_up is wrong)
        let ed = Haz3lcore.Editor.new_state(Pick_up, z, ed);
        Ok({
          ...model,
          id_gen,
          editors: Editors.put_editor(ed, model.editors),
        });
      };
    | Undo =>
      let ed = Editors.get_editor(model.editors);
      switch (Haz3lcore.Editor.undo(ed)) {
      | None => Error(CantUndo)
      | Some(ed) =>
        Ok({...model, editors: Editors.put_editor(ed, model.editors)})
      };
    | Redo =>
      let ed = Editors.get_editor(model.editors);
      switch (Haz3lcore.Editor.redo(ed)) {
      | None => Error(CantRedo)
      | Some(ed) =>
        Ok({...model, editors: Editors.put_editor(ed, model.editors)})
      };
    | MoveToNextHole(_d) =>
      // TODO restore
      Ok(model)
    | UpdateResult(key, res) =>
      /* If error, print a message. */
      switch (res) {
      | ResultFail(Program_EvalError(reason)) =>
        let serialized =
          reason |> EvaluatorError.sexp_of_t |> Sexplib.Sexp.to_string_hum;
        print_endline(
          "[Program.EvalError(EvaluatorError.Exception(" ++ serialized ++ "))]",
        );
      | ResultFail(Program_DoesNotElaborate) =>
        print_endline("[Program.DoesNotElaborate]")
      | _ => ()
      };
      let r =
        model.results
        |> ModelResults.find(key)
        |> ModelResult.update_current(res);
      let results = model.results |> ModelResults.add(key, r);
      Ok({...model, results});
    };
  reevaluate_post_update(update)
    ? m |> Result.map(~f=evaluate_and_schedule(state, ~schedule_action)) : m;
};
