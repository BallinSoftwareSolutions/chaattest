# Validation · Maxifying 0.7

The target is Windows 11 x64. This task uses Linux cross-compilation and Wine 10; Wine outcomes do not establish native Windows behavior or gaming performance.

## First-user review and 0.7

The first-user review found unclear switch semantics, a process-count hero that implied a performance target, no visible starting path, redundant profile application, and no convenient view of originals saved for restoration. The dashboard, explicit setting states, profile preflight and saved-only filter address those issues.

Added recent-game selection and explicit executable launch, plus per-game manually entered before/after FPS/1% low records with notes, validation, negative deltas, export and unsaved-edit guards. These are user-entered values, not measured game telemetry or proof of causality. The existing 23 settings, four profiles and restore primitives remain available.

- The new pure experience test checks truthful state classification, profile selection, invalid/implausible measurement rejection, regressions, recent-list limits/deduplication and stable per-game IDs.
- GUI checks roundtrip results and recent games through the real registry helpers in the isolated Wine prefix, then restore original values. They verify review before any individual tweak, saved-only filtering, keyboard access to all profiles and cancellation of the unsaved-game-switch warning.
- The catalog now has 400 unique three-language entries. Generated source and editable UTF-8 catalog are both checked in.
- GUI tests do not launch a game, apply profiles or uninstall apps. Native Windows executable-launch/launcher compatibility, actual game performance and native perceived frame pacing remain unverified.
- Screenshot capture now requests a fresh paint and compositor/GDI synchronization before reading pixels, so queued transition frames are not mistaken for settled screens.

## Language control placement

- The current-language button now sits at the top right beside the window controls. The footer is reserved for status. Ctrl+K still opens settings search.

## Added in 0.6

- 326 unique Danish/English/Swedish catalog entries, including every feature title, description, scope and detailed warning. `assets/translations.json` is the editable source; `scripts/generate-translations.py` generates the compiled catalog. Unknown external strings, user paths and historical activity are preserved.
- Pure localization tests check completeness, duplicate keys, three-language lookup and unchanged user paths. GUI tests operate the real language picker with keyboard input, read back each saved preference and restore the previous value in the isolated Wine prefix.
- DirectWrite text layouts and metrics use a bounded 512-entry cache keyed by text, dimensions, font and locale. Moving rows reuse layouts independently of their screen coordinates. UI smoke tests check zero new layouts and zero transition-composition passes on warm scroll frames.
- Steady scrolling presents the scene directly rather than copying it through the page-transition composition surface. Interrupted page transitions snapshot the last presented surface and still retain three render surfaces.
- Pagination is clamped to the last valid scroll position, including partial final pages. Motion tests cover the 23-item/6-visible case and shrinking lists.
- Category widths follow translated text; narrow button labels fit their available width.
- Nonblocking Direct2D presentation avoids waiting for a second presentation interval inside each paint. The animation timer follows the active monitor's reported refresh rate with a 4 ms lower limit and an 8 ms fallback. The timer stops at idle; native Windows frame pacing remains unverified.
- Expensive hardware identification is no longer repeated in the live statistics timer; lightweight process/RAM statistics refresh when animation is idle. The hardware summary is a snapshot refreshed explicitly or on language change.
- The new multi-size ICO and in-app emblem share the asset in `assets/maxifying-icon.png`.

## Downward-scroll rendering follow-up

- Animation timestamps use QueryPerformanceCounter rather than the coarse GetTickCount64 clock. The repaint timer requests 8 ms intervals; a balanced timeBeginPeriod/timeEndPeriod request exists only while the UI animation timer is running and is also released on destruction. This is UI pacing, not a persisted system/game timer tweak. Actual frame rate depends on Windows and the display.
- DirectWrite text disables pixel snapping so text and row backgrounds move together at fractional coordinates.
- The GUI smoke test sends downward WM_MOUSEWHEEL events through the real window procedure and checks intermediate movement plus fractional follow-up input. Wine validates this route, not native Windows perceived smoothness.

## Scroll polish after 0.5

- Critically damped scrolling keeps position and forward velocity continuous across wheel bursts, stops at bounds and responds immediately to reversals. Fractional wheel deltas are preserved. Dialog text now uses the same animation.
- Motion tests cover burst continuity, bounded travel, reversal, exact settling and fractional destinations.

## Added in 0.5

- Pure tests cover UTF-16 caret movement, complete-character length limits, malformed paste rejection and single-line control filtering.
- Optional-app policy tests reject protected/unknown package names, mismatched publisher identities and command injection. They verify current-user-only removal, framework/non-removable revalidation and post-removal readback in the generated script.
- Power-policy tests preserve unrelated flags and the original baseline while changing only execution-speed throttling.
- GUI smoke tests verify that cancelling a modal restores the original text-field focus/selection and that Tab skips window chrome.
- Native **EcoQoS getter/setter/restore** and **PowerShell/Appx inventory/removal** are unrun. Wine cannot validate these features. No app was actually uninstalled by the automated tests. Read-only inventory is cancellable; an active package deployment is never killed by the app.

## UI regression checks in 0.4

- Direct2D/DirectWrite single-surface renderer, with no native child controls on app pages or confirmation/error overlays.
- 30 rapid page switches reuse the same three allocated composition surfaces. A serial guard preserves the current composed image when navigation is interrupted.
- Explicit premultiplied-alpha surfaces remove the old text afterimage observed during testing.
- Font metrics, caret endpoints and Danish Unicode case folding are checked. The Wine environment lacks a working Segoe UI alias, so a validated DejaVu Sans fallback is used there. Windows normally uses Segoe UI.
- Navigation remains available while an artificial worker is pending. The UI test performs no real system optimization.
- Captured dashboard, settings, all four focus modes, network, restoration, search, category filters and custom confirmation/error dialogs from the running executable.
- Existing animation-math tests verify settling, bounds, interruption continuity and frame independence.

## Verified

- CMake configured and built the application, resources and test target successfully. CTest ran the Windows test executable through Wine: **6/6 tests passed**.

- Release executable built with MinGW, C++17, `-Wall -Wextra -Werror`, static C++ runtime, embedded icon, manifest and version information.
- Synthetic registry tests: missing original value, original binary data/type, repeated changes retaining the first baseline, repeated restore, backup collision rejection, GPU preference merge preserving unrelated tokens, fullscreen flags preserving existing compatibility flags, missing game executable rejection before backup, and unique/described feature catalog.
- Eight supported SystemParametersInfo settings applied and restored in an isolated Wine prefix. Each restored setting was read directly through the Windows API and compared with its exact saved baseline: mouse acceleration, pointer trails, pointer speed, minimize/maximize animation, menu animation, tooltip animation, client-area animation and cursor shadow.
- Three accessibility settings explicitly skipped because Wine reports the corresponding AVAILABLE flag unset: Sticky Keys, Toggle Keys and Filter Keys. Wine's source confirms the setters are unimplemented. The app marks such settings unavailable; these are not passing runtime tests.
- Screenshots are captured from the running Direct2D executable, not design mockups.
- Loopback ICMP test completed with 0/4 replies in this environment. The app showed the no-reply explanation and re-enabled its button. This validates error handling, not successful RTT measurement or external connectivity.

## Still requires native Windows 11 testing

- Real games: FPS, frametimes, input latency and compatibility.
- Accessibility setter behavior, display DPI changes, hardware GPU selection, browser-policy consumption, fullscreen behavior, power profiles/Modern Standby. Custom controls support keyboard navigation, but screen-reader/UI Automation integration is not implemented.
- Successful ICMP replies on an unrestricted Windows network; DNS flush under standard and restricted accounts.
- Process priority restoration, graceful app-close behavior and startup enable/disable with representative third-party applications. The registry backup primitives are tested; these end-to-end workflows remain unverified on Windows.

No test changes security mitigations, Windows Update, hardware interrupts or drivers. The default test suite uses synthetic keys only. The optional system-settings test must run in an isolated disposable environment.

## References

- [SystemParametersInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow)
- [Edge BackgroundModeEnabled](https://learn.microsoft.com/en-us/deployedge/microsoft-edge-browser-policies/backgroundmodeenabled)
- [Edge StartupBoostEnabled](https://learn.microsoft.com/en-us/deployedge/microsoft-edge-browser-policies/startupboostenabled)
- [Chrome BackgroundModeEnabled](https://chromeenterprise.google/policies/#BackgroundModeEnabled)
- [Wine 10 system parameters implementation](https://github.com/wine-mirror/wine/blob/wine-10.0/dlls/win32u/sysparams.c): mouse-speed uiParam handling and unimplemented accessibility setters explain the compatibility-specific test behavior.

Hone's website was blocked by this environment's network policy. Its current optimization catalog was not independently verified; the user-supplied list and reference image informed the scope and visual direction.
