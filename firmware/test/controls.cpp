// controls.cpp: checks the play page's FX and scene logic (FxControls.h, SceneControls.h)
// against a fake engine that records what it's sent. Exits 0 when everything passes. Run by
// controls.sh.
#include <cmath>
#include <cstdio>
#include "SceneControls.h"

using namespace chompi;

static int failures = 0;

static void Check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static bool Near(float a, float b) { return fabsf(a - b) < 1e-5f; }

/** Records what the controls send */
struct FakeEngine
{
    bool on[kNumFx] = {};
    float params[kNumFx][kNumFxParams] = {};
    int param_calls = 0;
    int fast_slews = 0;

    void SetFxOn(size_t fx, bool o) { on[fx] = o; }
    void SetFxParam(size_t fx, size_t p, float v)
    {
        params[fx][p] = v;
        param_calls++;
    }
    void FastFxSlew() { fast_slews++; }
};

using Fx = FxControls<FakeEngine>;
using Scenes = SceneControls<FakeEngine>;

static void Fresh(FakeEngine& e, Fx& fx)
{
    e = FakeEngine();
    fx.Init(&e);
}

static void TestInit()
{
    FakeEngine e;
    Fx fx;
    Fresh(e, fx);
    bool defaults = true, off = true;
    for (size_t f = 0; f < kNumFx; f++)
    {
        off = off && !e.on[f] && !fx.IsOn(f);
        for (size_t p = 0; p < kNumFxParams; p++)
            defaults = defaults && e.params[f][p] == kFxParams[f].defaults[p] &&
                       fx.Param(f, p) == kFxParams[f].defaults[p];
    }
    Check(defaults, "init: every parameter on its default, also in the engine");
    Check(off, "init: every FX off");
    Check(e.param_calls == static_cast<int>(kNumFx * kNumFxParams), "init: every parameter sent once");
}

static void TestKeys()
{
    FakeEngine e;
    Fx fx;
    Fresh(e, fx);

    fx.KeyPressed(FX_CRUSHER, true, false);
    Check(e.on[FX_CRUSHER] && fx.Selected() == FX_CRUSHER, "hold: on, and the knobs edit it");
    fx.KeyPressed(FX_CRUSHER, false, false);
    Check(!e.on[FX_CRUSHER], "release: off");

    // SHIFT first, then the key
    fx.KeyPressed(FX_FILTER, true, true);
    fx.KeyPressed(FX_FILTER, false, true);
    Check(e.on[FX_FILTER] && fx.IsLatched(FX_FILTER), "SHIFT + key: latched, stays on after release");

    // a plain press clears the latch but stays on while held
    fx.KeyPressed(FX_FILTER, true, false);
    Check(e.on[FX_FILTER] && !fx.IsLatched(FX_FILTER), "key on a latched FX: unlatched, on while held");
    fx.KeyPressed(FX_FILTER, false, false);
    Check(!e.on[FX_FILTER], "... and off on release");

    // the key first, then SHIFT
    fx.KeyPressed(FX_DELAY, true, false);
    fx.ShiftPressed();
    fx.KeyPressed(FX_DELAY, false, true);
    Check(e.on[FX_DELAY] && fx.IsLatched(FX_DELAY), "key, then SHIFT: latched");

    // SHIFT + key again unlatches
    fx.KeyPressed(FX_DELAY, true, true);
    fx.KeyPressed(FX_DELAY, false, true);
    Check(!e.on[FX_DELAY] && !fx.IsLatched(FX_DELAY), "SHIFT + key on a latched FX: unlatched");

    // SHIFT going down with no FX key held changes nothing
    fx.ShiftPressed();
    bool none = true;
    for (size_t f = 0; f < kNumFx; f++)
        none = none && !fx.IsLatched(f);
    Check(none, "SHIFT alone latches nothing");
}

static void TestKnobs()
{
    FakeEngine e;
    Fx fx;
    Fresh(e, fx);

    // continuous: 1% per detent, clamped
    fx.KeyPressed(FX_REVERB, true, false);
    const float decay = fx.Param(FX_REVERB, 0);
    fx.KnobTurned(0, 5.f, false);
    Check(Near(fx.Param(FX_REVERB, 0), decay + .05f), "fine: 1% per detent");
    Check(e.params[FX_REVERB][0] == fx.Param(FX_REVERB, 0), "fine: sent to the engine");
    fx.KnobTurned(0, 500.f, false);
    Check(fx.Param(FX_REVERB, 0) == 1.f, "fine: clamped at 1");

    // stepped: a step per 3 detents, snapped to the step grid
    fx.KeyPressed(FX_DELAY, true, false);
    const float step = 1.f / (DelaySend::kNumDivisions - 1);
    const float div = fx.Param(FX_DELAY, 0);
    fx.KnobTurned(0, 2.f, false);
    Check(fx.Param(FX_DELAY, 0) == div, "stepped: 2 detents do nothing yet");
    fx.KnobTurned(0, 1.f, false);
    Check(Near(fx.Param(FX_DELAY, 0), div + step), "stepped: the 3rd moves one step");
    fx.KnobTurned(0, -3.f, false);
    Check(Near(fx.Param(FX_DELAY, 0), div), "stepped: and back");

    // coarse, a spacing grid: the folder's drive in .2 steps
    fx.KeyPressed(FX_FOLDER, true, false);
    fx.KnobTurned(0, 3.f, false); // .5 -> .53, off the grid
    fx.KnobTurned(0, 1.f, true);
    Check(Near(fx.Param(FX_FOLDER, 0), .6f), "coarse: off the grid, snaps to the next point up");
    fx.KnobTurned(0, 1.f, true);
    Check(Near(fx.Param(FX_FOLDER, 0), .8f), "coarse: on a point, moves a whole step");
    fx.KnobTurned(0, 10.f, true);
    Check(Near(fx.Param(FX_FOLDER, 0), 1.f), "coarse: stops at the top");
    fx.KnobTurned(0, -1.f, true);
    Check(Near(fx.Param(FX_FOLDER, 0), .8f), "coarse: down a step");

    // coarse, a list of points: the shifter's intervals
    fx.KeyPressed(FX_SHIFTER, true, false);
    fx.KnobTurned(0, 1.f, true); // +7 -> +12
    Check(Near(fx.Param(FX_SHIFTER, 0), 1.f), "coarse points: +7 up to +12");
    fx.KnobTurned(0, 1.f, true);
    Check(Near(fx.Param(FX_SHIFTER, 0), 1.f), "coarse points: nothing above +12");
    fx.KnobTurned(0, -3.f, true); // +12 -> +7 -> +5 -> 0
    Check(Near(fx.Param(FX_SHIFTER, 0), .5f), "coarse points: three down from +12 is 0");

    // SHIFT + press resets, a plain press doesn't
    fx.KeyPressed(FX_REVERB, true, false);
    fx.KnobPressed(0, false);
    Check(fx.Param(FX_REVERB, 0) == 1.f, "plain press: nothing");
    fx.KnobPressed(0, true);
    Check(fx.Param(FX_REVERB, 0) == kFxParams[FX_REVERB].defaults[0], "SHIFT + press: the default");

    // the grid's helper on its own
    const FxGrid g = {0.f, .25f, nullptr, 0};
    Check(Near(Fx::CoarseStep(g, .5f, 1.f), .75f) && Near(Fx::CoarseStep(g, .5f, -1.f), .25f),
          "CoarseStep: a point moves to its neighbours");
    Check(Fx::CoarseStep(g, 0.f, -1.f) == 0.f && Fx::CoarseStep(g, 1.f, 1.f) == 1.f,
          "CoarseStep: nothing past 0 or 1");
}

static void TestScenes()
{
    FakeEngine e;
    Fx fx;
    Fresh(e, fx);
    FxScene store[kNumScenes] = {};
    Scenes sc;
    sc.Init(store, &fx);

    Check(sc.SlotPressed(0) == Scenes::Slot::EMPTY, "recall an empty slot: empty");

    // save the filter latched, its cutoff turned
    fx.KeyPressed(FX_FILTER, true, true);
    fx.KeyPressed(FX_FILTER, false, true);
    fx.KnobTurned(0, 10.f, false);
    const float cutoff = fx.Param(FX_FILTER, 0);
    sc.ModePressed(SceneMode::SAVE);
    Check(!sc.Armed(), "save: not armed before a slot");
    sc.SlotPressed(1);
    Check(sc.Armed() && sc.Selected() == 1, "save: armed on its slot");
    Check(sc.Confirm() == 1, "save: confirm changes slot 2");
    Check(store[1].used && (store[1].latched >> FX_FILTER & 1) && store[1].params[FX_FILTER][0] == cutoff,
          "save: the slot holds the latch and the knob");
    Check(sc.Active() == 1 && !sc.Edited() && sc.Mode() == SceneMode::NONE,
          "save: active, not edited, the mode ends");

    // change the sound, then recall
    fx.KnobTurned(0, 10.f, false);
    fx.KeyPressed(FX_FILTER, true, false);
    fx.KeyPressed(FX_CRUSHER, true, false);
    Check(sc.Edited(), "a change after saving: edited");
    fx.KeyPressed(FX_FILTER, false, false);
    const int calls = e.param_calls;
    Check(sc.SlotPressed(1) == Scenes::Slot::RECALL, "recall a saved slot");
    sc.Recall(1);
    Check(e.fast_slews == 1, "recall: at the fast slew");
    Check(e.param_calls - calls == 1, "recall: only the changed parameter sent");
    Check(fx.Param(FX_FILTER, 0) == cutoff && fx.IsLatched(FX_FILTER) && e.on[FX_FILTER],
          "recall: the knob and the latch come back");
    Check(e.on[FX_CRUSHER] && !fx.IsLatched(FX_CRUSHER), "recall: a key held stays on");
    Check(!sc.Edited() && sc.Active() == 1, "recall: active, not edited");
    fx.KeyPressed(FX_CRUSHER, false, false);

    // copy 2 to 3, then over the active one
    sc.ModePressed(SceneMode::COPY);
    Check(sc.SlotPressed(2) == Scenes::Slot::EMPTY, "copy: an empty source refused");
    sc.SlotPressed(1);
    Check(sc.Source() == 1 && !sc.Armed(), "copy: the source, not armed yet");
    sc.SlotPressed(1);
    Check(!sc.Armed(), "copy: the source again isn't a destination");
    sc.SlotPressed(2);
    Check(sc.Confirm() == 2 && store[2].used && store[2].params[FX_FILTER][0] == cutoff, "copy: 2 to 3");
    Check(!sc.Edited(), "copy elsewhere: still not edited");
    store[0] = store[2];
    store[0].params[FX_FILTER][0] = 0.f;
    sc.ModePressed(SceneMode::COPY);
    sc.SlotPressed(0);
    sc.SlotPressed(1);
    sc.Confirm();
    Check(sc.Edited() && sc.Active() == 1, "copy over the active scene: edited");

    // mode keys
    sc.ModePressed(SceneMode::DELETE);
    sc.ModePressed(SceneMode::DELETE);
    Check(sc.Mode() == SceneMode::NONE, "the mode key again cancels");
    sc.ModePressed(SceneMode::DELETE);
    sc.SlotPressed(1);
    sc.ModePressed(SceneMode::SAVE);
    Check(sc.Mode() == SceneMode::SAVE && sc.Selected() == kNoScene, "another mode key switches, the slot cleared");
    Check(sc.Confirm() == kNoScene, "confirm without a slot: nothing");

    // delete the active one
    sc.ModePressed(SceneMode::SAVE);
    sc.ModePressed(SceneMode::DELETE);
    sc.SlotPressed(1);
    Check(sc.Confirm() == 1 && !store[1].used && sc.Active() == kNoScene, "delete the active scene");
    sc.ModePressed(SceneMode::DELETE);
    Check(sc.SlotPressed(1) == Scenes::Slot::EMPTY, "delete: an empty slot refused");
}

int main()
{
    TestInit();
    TestKeys();
    TestKnobs();
    TestScenes();
    if (failures)
        printf("%d failed\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
