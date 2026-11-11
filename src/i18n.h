// 界面文案。中文先跑通，英文列留空，以后补。
#pragma once

namespace lb {

enum class Lang { Zh, En };

enum class S {
    AppTitle,
    AppTagline,
    NewBlank,
    NewDefault,
    OpenScene,
    Demo,
    Recent,
    NoRecent,
    NoDemoDir,
    EditorHint,
    BtnPause,
    BtnResume,
    BtnReset,
    BtnResetToSaved,
    BtnSave,
    BtnSaveAs,
    BtnAdvanced,
    BtnEditSave,
    SectionWorld,
    SectionLine,
    SectionBall,
    SectionEmitter,
    SectionHints,
    SectionMulti,
    BtnDeleteLine,
    BtnDeleteBall,
    BtnDeleteEmitter,
    BtnDeleteSelected,
    BtnCopy,
    BtnPaste,
    Unsaved,
    Saved,
    MenuResume,
    MenuToTitle,
    MenuQuit,
    MenuUnsavedTitle,
    MenuUnsavedBody,
    MenuSaveAndGo,
    MenuDiscard,
    MenuCancel,
    Count,
};

void setLanguage(Lang l);
Lang language();
const char* tr(S key);

} // namespace lb
