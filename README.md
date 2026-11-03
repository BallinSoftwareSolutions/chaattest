# GameTune – Windows 11

## Hent programmet

**[Download GameTune.exe](https://github.com/BallinSoftwareSolutions/chaattest/raw/refs/heads/gametune-download/downloads/GameTune.exe)**

Hvis direkte download ikke virker: [åbn filen på GitHub](https://github.com/BallinSoftwareSolutions/chaattest/blob/gametune-download/downloads/GameTune.exe), og klik på **Download raw file** (pilen ned). Er repository'et privat, skal du være logget ind på GitHub med adgang til det.

Find filen i **Downloads**, og åbn **GameTune.exe** på din Windows 11-pc.

## Funktioner

Native C++17-app uden database. Mørkt dashboard med gyldne accenter og indstillingskort.

- Gamingprofil med bekræftelse: Game Mode til og Game DVR-optagelse fra.
- Game Mode og optagelse kan også ændres enkeltvis.
- Høj ydeevne-strømprofil med gendannelse. Kan øge varme og strømforbrug; understøttes ikke på alle pc'er.
- Genvej til Windows-grafikindstillinger for GPU-valg, HAGS og VRR, hvor understøttet.
- Originale registerværdier og strømprofil gemmes i HKCU/Software/GameTune/Backup og kan gendannes efter appgenstart.

Ingen ændringer ved appstart. Ingen deaktivering af sikkerhed eller opdateringer. Ingen garanteret FPS- eller latencyforbedring. Mål samme spil og scene før/efter. Genstart spillet efter registerændringer. Flere optimizer-apps kan overskrive hinandens ændringer.

## Valideringsstatus

Windows x64-app krydskompileret fra Linux med `-Wall -Wextra -Werror` og statisk C++-runtime. Importerer kun Windows-system-DLL'er. Windows-kørsel, visuel rendering, strømprofiladfærd og gaming-performance er endnu ikke valideret. Dette er en tidlig version, ikke en dokumenteret performanceforbedring.

## Byg selv

Windows: Visual Studio 2022 Build Tools med Desktop development with C++ og CMake. Fra Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\GameTune.exe
```

Testen bruger isolerede syntetiske registernøgler. Den kontrollerer manglende originalværdier, datatype og binære data samt gentagne ændringer; den ændrer ikke gaming-indstillinger. Testen kræver Windows.

Linux med MinGW: `GAMETUNE_CXX=x86_64-w64-mingw32-g++ sh scripts/build-linux.sh`. I det forberedte cloud-miljø ligger compileren under `/workspace/mingw` og vælges automatisk.

Den leverede .exe og dens SHA-256 er i `downloads/`. Binæren er gemt i Git på denne downloadgren, så den kan hentes uden adgang til et midlertidigt cloud-miljø.
