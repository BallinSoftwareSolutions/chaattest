# Maxifying 0.6 · Performance Studio

**[Download Maxifying.exe](https://github.com/BallinSoftwareSolutions/chaattest/raw/refs/heads/gametune-download/downloads/Maxifying.exe)**

Hvis download ikke starter: [åbn filen på GitHub](https://github.com/BallinSoftwareSolutions/chaattest/blob/gametune-download/downloads/Maxifying.exe), og klik på pilen **Download raw file**. Log ind på GitHub, hvis repository'et er privat. Find derefter filen i Downloads på din Windows 11-pc.

## Nyt i 0.6

- **Dansk, English og Svenska:** åbn sprogknappen nederst til højre. Navigation, featureforklaringer, dialoger og advarsler skifter uden genstart. Valget gemmes under din Windows-bruger. Egne filnavne, programnavne, Windows-fejl og tidligere historik beholdes som de er.
- **Scroll-rendering:** tekstlayout og målinger genbruges i en begrænset cache. Scroll springer den ekstra sidekomposition over, og opdateringsintervallet følger skærmens rapporterede Hz. Nye hjulinput reagerer hurtigere, og hardwareinspektion køres ikke længere hvert andet sekund. Den faktiske oplevelse skal stadig vurderes på Windows 11 og din skærm.
- **Nyt ikon:** et symbol i sølv og blå på mørk baggrund erstatter det grønne M i både programfil, proceslinje og appens header.

## Nyt i 0.5

- **EcoQoS pr. spilproces:** under Fokus → Kørende apps kan execution-speed throttling slås fra for den valgte proces, når Windows understøtter det. Andre power-flags bevares. Originaltilstanden gendannes sammen med prioriteten ved normal applukning eller via Gendan session. Et crash kan efterlade sessionsvalget aktivt, indtil spillet afsluttes. Ingen globale timer-, HPET- eller driverændringer, og ingen garanteret latencygevinst.
- **Valgfrie Windows-apps:** Fokus → Windows-apps læser den aktuelle brugers pakker via Windows PowerShell/Appx og viser kun ni kendte valgfrie appnavne. Den valgte pakke kontrolleres igen før fjernelse, og resultatet læses tilbage. Store, frameworks, sikkerhed og gaming-tjenester er ikke i listen. Ingen `-AllUsers` eller fjernelse af provisionerede systempakker.
- **Afinstallation er ikke en backup-funktion:** appdata kan slettes, og Gendan alle geninstallerer ikke apps. Handlingen kræver en særskilt bekræftelse med pakkens identitet. Store-linket er en mulighed for geninstallation, ikke gendannelse af lokale data. Windows-opdateringer eller nye brugere kan få apps installeret igen.
- **UI-polish i den eksisterende stil:** dialoger giver fokus og markering tilbage til tekstfeltet; Tab vælger ikke vinduets luk-knap; clipboard-fejl sletter ikke markeret tekst; Unicode-tegn deles ikke ved feltets længdegrænse.

Installerede apps koster især gaming-performance, hvis de også kører eller starter i baggrunden. Afinstallation alene dokumenterer ikke lavere input-latency. Processantal og RAM er observerede Windows-værdier, ikke en FPS-score.

De nye EcoQoS- og Appx-forløb kræver native Windows 11-afprøvning. Wine viser dem som utilgængelige; det er ikke et bestået funktionstestresultat.

## Hvad er nyt?

Maxifying hed tidligere GameTune. Eksisterende backups og historik genbruges, så du stadig kan gendanne gamle ændringer.

Et native C++17-dashboard med mørkt design, lime/lilla accenter, topnavigation, søgning, aktuelle statusværdier, detaljer pr. indstilling og bekræftelse før ændringer. Vinduet skaleres ned, så det passer på mindre skærme. Ingen konto eller database.

| Kategori | Individuelle indstillinger |
|---|---|
| Gaming · 3 | Game Mode, Game DVR/App Capture fra, høj ydeevne-strømprofil |
| Input · 6 | Museacceleration, markørspor, neutral musehastighed, Sticky Keys-genvej, Toggle Keys-genvej, Filter Keys |
| Windows · 7 | Gennemsigtighed, vinduesanimation, menuanimation, tooltip-animation, indholdsanimation, markørskygge, versionsafhængige web-søgeforslag |
| Baggrund · 3 | Edge-baggrundstilstand, Edge Startup Boost, Chrome-baggrundstilstand |
| Privatliv · 2 | Windows-reklame-id og tilpassede oplevelser med diagnosedata |
| Spilprofil · 2 | Højtydende GPU-præference og valgfri fullscreen-kompatibilitetstest for en valgt .exe |

**23 indstillinger**, plus følgende værktøjer:

- **Fire profiler:** gaming, input, fokus og minimal desktop. Fokus slår Edge/Chrome-baggrundstilstand og Edge Startup Boost fra; genstart browserne bagefter. En dialog viser de præcise ændringer før godkendelse. Power-plan, Filter Keys og fullscreen-eksperimenter medtages ikke automatisk.
- **Netværk:** fire ICMP-pakker til en valgfri IPv4-adresse med RTT og pakketab, håndtering af manglende svar samt DNS-cache-rydning via Windows' ipconfig. Ping måler netværks-RTT, ikke input-latency.
- **Hardware:** CPU, logiske processorer, RAM-forbrug, skærmadapter, opløsning, opdateringsfrekvens samt aktuelle proces-, tråd- og handle-antal.
- **Procesprioritet:** Above Normal til en konkret kørende app med et synligt hovedvindue (også minimeret). Den originale prioritet gendannes ved normal applukning eller manuelt. Ved appcrash varer ændringen indtil spilprocessen afsluttes. Ingen Realtime-prioritet eller automatisk CPU-affinity.
- **Luk valgt app:** sender en normal lukkeanmodning til den valgte apps synlige hovedvinduer efter bekræftelse. Appen kan bede om at gemme eller vælge at blive i systembakken. Processer tvangsafsluttes ikke; kendte Windows-systemprocesser filtreres fra listen.
- **Opstart:** deaktiver/gendan navngivne opstartsposter i den aktuelle brugers Run-nøgle. Programmet afinstalleres ikke. Task Managers godkendelser, tjenester, planlagte opgaver og andre brugere er uden for denne liste.
- **Gendannelse:** individuelle backups, gendan alle inklusive tidligere valgte spil og opstartsposter, vedvarende aktivitetshistorik og eksport af en tekstrapport.

## Ny brugerflade i 0.4

- Ny arbejdsflade med topnavigation, redaktionelt overblik, indstillingsliste og separat detaljepanel. De 23 indstillinger og fire profiler er bevaret.
- Hele UI'en tegnes i én Direct2D-flade med DirectWrite-tekst. Knapper, switches, søgefelter og bekræftelses-/fejldialoger er del af samme frame; der er ingen native børnekontroller, som kan blinke frem mellem sider.
- Tre genbrugte renderflader sammensætter komplette sider med korte overgange. Pixel-formatet er eksplicit premultiplied alpha, så gamle tekstkonturer ikke hænger efter.
- Egne dialoger viser effekt, originalbackup og tydelige Annuller/Anvend-knapper. Escape annullerer. OS-filvælgere bruges fortsat til valg af spil og rapportplacering.
- Søgning via Ctrl+K, tastaturnavigation, tekstmarkering, kopi/indsæt og glidende listesrulning. Tekstfelternes caret bruger DirectWrite-metrik.
- Arbejdet udføres stadig asynkront. Succes vises først, når ændringen er gennemført og læst tilbage. Animationstimeren stopper ved stilstand.
- Windows' Segoe UI foretrækkes. En fallback vælges ud fra en faktisk glyph-layout-test, hvis fontnavnet ikke kan tegnes i test-/kompatibilitetsmiljøer.

Færre processer er ikke i sig selv et mål for højere FPS. Fokusprofilen, opstartsoversigten og lukkeanmodninger reducerer kun den valgte baggrundsaktivitet; appen ombygger ikke Windows eller lukker alle systemprocesser.

## Virkning og kontrol

Appen ændrer intet ved opstart. Den gemmer første originalværdi med datatype og data i `HKCU/Software/GameTune/Backup`. Gendannelse bevarer også oprindeligt manglende registerværdier. Gamle backups fra første GameTune-version understøttes. Undgå at lade flere optimizer-apps styre samme indstilling samtidig: gendannelse bruger den første gemte original, ikke eventuelle senere ændringer.

Hver ændring forklares i **Info**. Windows-indstillinger læses tilbage; system-API-ændringer bliver ikke markeret som gennemført, hvis de ikke kan bekræftes. Et registerflag beviser ikke, at et spil, en browser eller en organisationspolitik følger flaget. Browserpolitikker kræver browsergenstart og kan overstyres af organisationens politik. Game Mode/GPU/compatibility kræver typisk genstart af spil; visse Windows-politikker kan kræve ny login-session.

Strømprofilen kan være utilgængelig på Modern Standby-maskiner. GPU-præferencer er ønsker til Windows og kan overstyres af driver/spil. Fullscreen-eksperimentet kan forringe ydelse, VRR og Alt-Tab og vælges kun manuelt. Tilgængelighedsfunktioner vises utilgængelige, når Windows ikke rapporterer understøttelse.

FPS og input-latency afhænger af spil og hardware. Visuelle præferencer og privatlivsvalg er ikke dokumenterede FPS-forbedringer. Appen foretager ikke automatisk HPET-/timer-, MSI-, interrupt-, driver-, CSRSS-, TCP-, sikkerhedsmitigerings- eller Windows Update-ændringer. Den lover ikke risikofri tuning eller Hones fulde funktionsparitet.

## Byg på Windows

Visual Studio 2022 Build Tools med Desktop development with C++ og CMake. Fra Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\Maxifying.exe
```

Standardtesten bruger isolerede syntetiske registernøgler og ændrer ikke gaming-indstillinger. `MaxifyingTests.exe --system-settings` anvender og gendanner rigtige mus-/tastatur-/animationsindstillinger og bør kun køres i en disponibel testkonto eller VM.

## Linux / cloud

```sh
sh scripts/build-linux.sh
sh scripts/build-tests-linux.sh
```

Forberedt compiler: `/workspace/mingw/usr/bin/x86_64-w64-mingw32-g++-posix`. En anden MinGW-compiler og resource-compiler kan vælges via `MAXIFYING_CXX` og `MAXIFYING_RC` (de gamle `GAMETUNE_*`-navne accepteres også). Appen indeholder ikon, versionsmetadata og Windows-manifest og bruger statisk C++-runtime. `downloads/GameTune.exe` bevares som en identisk kompatibilitetskopi til gamle links. Output og checksum ligger i `downloads/` og er gemt i Git, så download ikke afhænger af cloud-sessionen.

## Validering

Luk den tidligere GameTune-version, før du åbner Maxifying. Den gamle downloadadresse leverer også den nye version.

Se [testresultater og begrænsninger](docs/VALIDATION.md). Brugerfladen og understøttede indstillingsændringer er afprøvet i Wine; native Windows 11 og FPS/latency i rigtige spil mangler stadig test.

![Overblik i Wine-testmiljø](docs/overview.png)
