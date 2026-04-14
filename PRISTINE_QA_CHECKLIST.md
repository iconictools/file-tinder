# File Tinder — Pristine Quality Checklist

Use this as a release gate. A release is **not pristine** until every item is checked.

## 0) Global release gate

- [ ] No crash, freeze, or silent failure in Launcher, Basic, Advanced, AI, Diagnostics, File List, Duplicate Detection, Image Preview
- [ ] Every invalid action is blocked with a clear user-facing message
- [ ] Every destructive action requires explicit confirmation
- [ ] Every busy/async operation has progress + cancel path
- [ ] Session and DB stay consistent across close/reopen and forced interruption

---

## 1) Launcher (main window)

- [ ] Source folder picker: valid path, cancel, missing path, no-permission path
- [ ] Recent folders: open, remove (middle-click), persistence after restart
- [ ] Mode entry checks (Basic/Advanced/AI) enforce folder preconditions
- [ ] Empty-folder path shows message and blocks start
- [ ] Session overview counts are accurate
- [ ] Clear Session confirmation and post-clear UI refresh
- [ ] Undo History open, per-row undo, permanent rows disabled
- [ ] Theme toggle text and persistence across restart
- [ ] Diagnostics opens/closes cleanly

---

## 2) Basic mode (all controls and flows)

- [ ] Buttons: Keep / Delete / Skip / Undo / Preview / Reset / File List / Finish map to correct actions
- [ ] Duplicate button (`!`) appears only when duplicates exist and opens duplicate window
- [ ] Switch Mode menu (Advanced / AI) preserves state
- [ ] Help dialog content matches current shortcuts/behavior
- [ ] Keyboard: arrows, Z, Backspace, P, F, Ctrl+F, Enter, ?, Esc
- [ ] Search success/failure feedback is explicit
- [ ] Filter: All / Images / Videos / Audio / Documents / Archives / Other / Folders Only / Specify
- [ ] Custom extension specify dialog: add/remove/cancel/reapply behavior
- [ ] Include Folders toggle and rescans keep counts/session coherent
- [ ] Sort field + Asc/Desc preserve expected ordering and selection continuity
- [ ] Close/reject Save/Discard/Cancel paths are re-entrant safe

---

## 3) Advanced mode (grid/mind-map circuits)

- [ ] Grid renders root/children correctly across small and large trees
- [ ] Add folder flows: create new, add existing, add subfolder, replace, remove
- [ ] Context actions: move file here, add quick access, open folder, color, remove
- [ ] Quick access slots (1-0) assign/load/persist correctly
- [ ] Quick access truncation still preserves full-path tooltip
- [ ] Grid controls: compact/full paths/A-Z/#/save/load/reset/presets
- [ ] Keyboard grid navigation (tab/arrows/space/enter/esc) never goes out of bounds
- [ ] Keep/Delete/Skip/Undo/Finish maintain assignment counts and DB state
- [ ] Missing/unwritable destination folders are detected and messaged

---

## 4) AI mode (setup + analysis + rerun)

- [ ] AI Setup validates API key rules, endpoint required, model selection path
- [ ] Fetch models behavior and error messaging for each provider
- [ ] Auto mode and Semi mode both execute and review correctly
- [ ] Category handling (Keep Existing / Generate New / Synthesize / Keep+Generate) behaves as configured
- [ ] Confidence threshold filtering works only in Semi mode UI suggestions
- [ ] Rate limiting/backoff keeps app responsive and cancelable
- [ ] Response parsing fallback paths (strict JSON / extracted array / line-by-line) are robust
- [ ] Category review dialog apply/cancel behavior is deterministic
- [ ] Re-run AI menu: Remaining / All overwrite / Re-categorize do exactly what labels state
- [ ] AI reasoning shown when available; hidden cleanly when unavailable

---

## 5) AI compatibility matrix (must verify explicitly)

### 5.1 Provider, auth, endpoint, fetch behavior

| Provider | API key required to start | Fetch models endpoint logic | Request auth/header rule | Compatible status |
|---|---|---|---|---|
| OpenAI | Yes | Derived `/v1/models` from endpoint | `Authorization: Bearer` | [ ] |
| Anthropic | Yes | Fixed `https://api.anthropic.com/v1/models` | `x-api-key` + `anthropic-version` | [ ] |
| Google Gemini | Yes | `.../v1beta/models?key=...` (blocks fetch if key empty) | Key in query for generation URL | [ ] |
| Mistral | Yes | OpenAI-compatible `/v1/models` derivation | `Authorization: Bearer` | [ ] |
| Groq | Yes | OpenAI-compatible `/v1/models` derivation | `Authorization: Bearer` | [ ] |
| OpenRouter | Yes | Fixed `https://openrouter.ai/api/v1/models` | `Authorization: Bearer` | [ ] |
| Ollama (Local) | No | Fixed `http://localhost:11434/api/tags` | No key required | [ ] |
| LM Studio (Local) | No | Fixed `http://localhost:1234/v1/models` | No key required | [ ] |
| Custom | Depends on config | Attempts endpoint-based OpenAI-compatible logic | Bearer unless Gemini-like custom path chosen | [ ] |

### 5.2 Sort mode × category mode compatibility

| Combination | Expected behavior | Must be blocked/adjusted? | Verified |
|---|---|---|---|
| Auto × Keep Existing | Assigns first valid existing folder per file | If folder grid empty, auto-switch to Generate New + warning | [ ] |
| Auto × Generate New | Generates new folders under source root | Block paths outside source root | [ ] |
| Auto × Synthesize New | Uses existing intent + creates improved categories | If grid empty, falls back to Generate New | [ ] |
| Auto × Keep+Generate | Keeps existing and adds new | New paths must remain under source root | [ ] |
| Semi × Keep Existing | Highlights top N existing suggestions | Invalid suggestions must be ignored | [ ] |
| Semi × Generate New | Suggests new folders; user chooses | New folder review/edit flow must work | [ ] |
| Semi × Synthesize New | Suggest + synthesize from existing intent | Empty-grid fallback must be deterministic | [ ] |
| Semi × Keep+Generate | Mix existing + new suggestions | Review/add path integrity required | [ ] |

### 5.3 Re-run AI option compatibility

| Re-run option | Allowed when | Expected behavior | Verified |
|---|---|---|---|
| Remaining unsorted files only | AI configured | Reanalyzes only `pending` files | [ ] |
| All files (overwrite existing decisions) | AI configured | Resets prior decisions and reanalyzes all | [ ] |
| Re-categorize | AI configured and moved files exist | Prompts confirm, resets moved files to pending, reruns on remaining path | [ ] |
| Any re-run while AI not configured | Never | Must block with “AI Not Configured” message | [ ] |
| Re-categorize with zero moved files | Never | Must block with “No Sorted Files” message | [ ] |

### 5.4 Non-AI compatibility matrix (other parts)

| Area | Combination | Expected behavior | Must block/message | Verified |
|---|---|---|---|---|
| Mode switching | Basic → Advanced | Decisions preserved; advanced grid opens same session | If unsaved close path interrupted, require explicit save/discard/cancel | [ ] |
| Mode switching | Basic → AI | AI setup required before analysis | Block rerun/setup-dependent actions until configured | [ ] |
| Mode switching | Advanced → Basic | Decisions preserved; no assignment loss | If current selection invalid after filter change, move to safe next index | [ ] |
| Mode switching | Advanced ↔ AI | Folder model + decisions remain coherent | If incompatible category choice selected, auto-adjust with warning | [ ] |
| Filter + include | Folders Only + Include Folders | Include checkbox disabled | Must not silently keep stale include state behavior | [ ] |
| Filter + custom | Specify with empty extension list | Revert to previous/All behavior | Must message/visibly revert (never apply empty custom filter silently) | [ ] |
| Sort + order | Any sort field + Asc/Desc toggle | Deterministic ordering and stable navigation | No index-out-of-range when current filtered item disappears | [ ] |
| File List selection | Ctrl/Shift multi-select + single-click navigation | Multiselect actions only on selected set | Must not auto-navigate unexpectedly during multi-select operations | [ ] |
| Duplicate detection | Verify hash then delete selected | Selection maps exactly once to marks | Must not double-mark or lose mark state after reopen | [ ] |
| Review + execute | Bulk edit + execute + undo history | Counts, DB rows, and undo eligibility stay aligned | Permanent actions must be visibly non-undoable | [ ] |

---

## 6) File List window

- [ ] Filter field updates deterministically
- [ ] Single-click navigation and Ctrl/Shift multiselect interactions are correct
- [ ] Context “Move selected to” applies only to selected rows
- [ ] Counters (shown/total/selected) remain accurate
- [ ] No duplicate signal effects after repeated refresh/filter cycles

---

## 7) Duplicate Detection window

- [ ] Name+size grouping is correct
- [ ] Hash verification state transitions and button labels are correct
- [ ] Delete Selected marks exact files once (no double-marking)
- [ ] Reopen reflects current decisions

---

## 8) Image Preview window

- [ ] Prev/Next boundaries
- [ ] Zoom in/out, fit, 1:1, slider synchronization
- [ ] Corrupt/invalid images show clear failure text and recover on next valid file

---

## 9) Review and execute pipeline

- [ ] Summary counts and destinations match in-memory and DB state
- [ ] Bulk actions (All Keep/All Skip/All Pending) sync UI + DB + counters
- [ ] Execute All handles partial failures with actionable error reporting
- [ ] Name collision handling is deterministic
- [ ] Trash vs permanent delete behavior is explicit and confirmed
- [ ] Execution-complete stats match actual operation outcomes
- [ ] Undo entries are available only when truly reversible

---

## 10) Persistence and DB integrity

- [ ] Session resume is correct per source folder
- [ ] Filter/sort/grid/quick-access/provider settings restore correctly
- [ ] Clear session/history operations are scoped correctly
- [ ] Fresh DB creation and schema evolution do not corrupt data
- [ ] External file/folder moves do not leave stale, silent-invalid state

---

## 11) Must-block scenarios (never silently allow)

For each case below, verify user sees: **what failed + why + next step**.

- [ ] No source folder selected
- [ ] Source folder missing/unreadable
- [ ] Empty/unsupported source content for selected action
- [ ] Destination missing/unwritable
- [ ] File changed/deleted externally during session
- [ ] Name collision exceeded retry strategy
- [ ] Missing API key for cloud provider
- [ ] Endpoint timeout/unreachable/429/5xx/invalid response format
- [ ] Undo impossible (e.g., permanent deletion)

---

## 12) Performance, stability, UX, security final pass

- [ ] Large folder scan (10k+) remains responsive
- [ ] Duplicate hashing remains responsive and cancel-safe
- [ ] Long AI run remains responsive (progress + cancellation usable)
- [ ] Repeated mode switches do not leak state/windows/signals
- [ ] Button labels/tooltips are unambiguous
- [ ] Keyboard-only workflow is complete in each mode
- [ ] Sensitive values (API key/logs) are not unintentionally exposed

---

## 13) Final acceptance

- [ ] Full manual regression completed across all modes/tools
- [ ] All high-confidence defects found during checklist run are resolved
- [ ] README/help content reflects actual behavior
- [ ] Release marked “pristine” only when every section above is complete
