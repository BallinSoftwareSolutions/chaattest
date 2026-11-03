# Validation · Maxifying 0.3

The target is Windows 11 x64. This task uses Linux cross-compilation and Wine 10; Wine outcomes do not establish native Windows behavior or gaming performance.

## UI regression checks in 0.4

- Direct2D/DirectWrite single-surface renderer, with no native child controls on app pages or confirmation/error overlays.
- 30 rapid page switches reuse the same three allocated composition surfaces. A serial guard preserves the current composed image when navigation is interrupted.
- Explicit premultiplied-alpha surfaces remove the old text afterimage observed during testing.
- Font metrics, caret endpoints and Danish Unicode case folding are checked. The Wine environment lacks a working Segoe UI alias, so a validated DejaVu Sans fallback is used there. Windows normally uses Segoe UI.
- Navigation remains available while an artificial worker is pending. The UI test performs no real system optimization.
- Captured dashboard, settings, all three focus modes, network, restoration, search, category filters and custom confirmation/error dialogs from the running executable.
- Existing animation-math tests verify settling, bounds, interruption continuity and frame independence.

## Verified

- CMake configured and built the application, resources and test target successfully. CTest ran the Windows test executable through Wine: **2/2 tests passed**.

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
