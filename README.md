# MiniSharp

Mały język w stylu C#, który **kompiluje się i uruchamia na samym Nintendo 3DS**.
Kompilator i maszyna wirtualna są napisane w czystym C (ok. 2500 linii), bez żadnych zależności poza libctru i citro2d.

```csharp
class Game {
    static float x = 200;

    static void Update() {
        if (Input.Held(Btn.Left)) x -= 3;
        if (Input.Held(Btn.Right)) x += 3;
    }

    static void Draw() {
        Gfx.Clear(Color.Black);
        Gfx.Circle(x, 120, 12, Color.Yellow);
        Gfx.Text(10, 10, "x = " + (int)x, Color.White);
    }
}
```

## Jak to działa

```
plik .cs ──► lekser ──► parser (AST) ──► sprawdzanie typów + generowanie bytecode'u ──► VM
```

- **Statyczne typy.** Kompilator zna typ każdego wyrażenia, więc VM ma osobne instrukcje dla `int` i `float` (`IADD`, `FADD`…) i nie sprawdza typów w trakcie działania.
- **Bytecode na stosie.** Prosta, przenośna maszyna wirtualna. Na 3DS nie trzeba żadnych sztuczek z pamięcią wykonywalną.
- **Garbage collector.** Mark & sweep, bez rekurencji (bezpieczny przy długich listach obiektów).
- **Czytelne błędy** z numerem linii, np. `line 12: cannot convert float to int automatically, use (int)`.

## Na 3DS

1. Zbuduj `MiniSharp.3dsx` (niżej) i skopiuj do `sdmc:/3ds/`.
2. Uruchom z Homebrew Launchera. Przy pierwszym starcie aplikacja tworzy folder `sdmc:/minisharp/` i wrzuca tam przykłady.
3. Wybierz plik D-padem, **A** = kompiluj i uruchom. **START** w grze wraca do menu, **START** w menu zamyka aplikację. **Y** odświeża listę plików.

Własne programy wrzucasz jako pliki `.cs` do `sdmc:/minisharp/` (przez czytnik kart albo FTP, np. ftpd).
Wyjście z `Console.WriteLine` widać na dolnym ekranie w menu.

Log z każdego uruchomienia (wyjście `Console`, błędy, czasy klatek) zapisuje się w `sdmc:/minisharp/log.txt`.

### Pisanie gier w VS Code

Otwórz folder `minisharp/` w VS Code (z rozszerzeniem C#). `MiniSharp.Games.csproj` i `sdk/MiniSharp.Api.cs` dają IntelliSense dla `Gfx`, `Input` itd. Ten projekt służy tylko do podpowiedzi, prawdziwym kompilatorem jest MiniSharp.

- Gry trzymaj w `games/`; każda w osobnym pliku z własnym `namespace` (szablon: `games/MojaGra.cs`).
- **Ctrl+Shift+B**: sprawdza plik MiniSharpem (błędy w panelu Problems) i wysyła go przez FTP do `sdmc:/minisharp/`.
- *Terminal → Run Task*: „podgląd na PC” (zrzut klatki jako PNG), „pobierz log z 3DS”, „zbuduj i wyślij aplikację”.
- Adres konsoli: zmienna `MINISHARP_3DS` (domyślnie `192.168.1.90:5000`).

Rozszerzenie C# zna pełny C#, a MiniSharp tylko podzbiór, więc to, czego jeszcze nie ma (np. `foreach`), wyłapie dopiero Ctrl+Shift+B.

### Budowanie

**GitHub Actions (najprościej):** wrzuć repo na GitHuba. Workflow `.github/workflows/build.yml` buduje `.3dsx` w kontenerze `devkitpro/devkitarm` i odpala testy. Gotowy plik pobierzesz z zakładki *Actions → ostatni build → Artifacts*.

**Lokalnie z devkitPro:**
```
make                       # MiniSharp.3dsx
```

**Wersja na PC (do testów):**
```
make -f Makefile.pc test   # buduje ./minisharp i odpala testy
./minisharp examples/hello.cs
./minisharp examples/snake.cs --frames 200 --ppm klatka.ppm   # zapisuje obraz obu ekranów
./minisharp plik.cs --dis  # pokazuje wygenerowany bytecode
```

## Język (wersja 2)

### Co jest
- **Typy:** `int`, `float` (`double` to alias), `bool`, `char`, `string`, `void`, tablice `T[]` (także `int[][]`), `List<T>` (także `List<List<int>>`), własne klasy.
- **Klasy:** pola (instancyjne i `static`, z wartościami początkowymi), metody, jeden konstruktor, `this`, `new`, `null`. `const` działa jak pole statyczne.
- **Instrukcje:** `if`/`else`, `while`, `do`/`while`, `for`, `foreach` (tablice, listy, stringi), `switch` (int, char, string, bool; `case 1: case 2:`, `default`), `break`, `continue`, `return`, bloki, `var`, `int a = 1, b = 2;`.
- **Wyrażenia:** `+ - * / %`, `== != < <= > >=`, `&& || !`, bitowe `& | ^ ~ << >>`, `? :`, `= += -= *= /= %= &= |= ^= <<= >>=`, `++ --` (przed i po), rzutowania `(int)` / `(float)` / `(char)`.
- **Stringi:** łączenie `+` z dowolnym typem (`"x=" + x`), `.Length`, `s[i]` (char), porównanie `==`, `Substring IndexOf Contains StartsWith EndsWith ToUpper ToLower Trim Replace Split`, `string.IsNullOrEmpty`, `x.ToString()`.
- **char:** literały `'A'`, `'\n'`; `char.IsDigit IsLetter IsWhiteSpace ToUpper ToLower`; `c + 1` daje `int`, `c++` zostaje `char` (jak w C#). Znaki są 8-bitowe (ASCII), polskie litery w stringach liczą się jako kilka znaków.
- **Liczby:** `int.Parse(s)`, `float.Parse(s)`, `int.MaxValue`, `int.MinValue`.
- **List<T>:** `new List<int>()`, `new List<string> { "a", "b" }`, `new List<T>(innaListaLubTablica)`, `Add AddRange Insert Remove RemoveAt Clear Contains IndexOf`, `.Count`, `lista[i]`, `foreach`.
- **Dictionary<K,V>:** klucze `int`/`string`/`char`/obiekty, `d[k]` (też `d[k] += 1`), `Add Remove ContainsKey ContainsValue Clear`, `TryGetValue(k, out var v)`, `.Count .Keys .Values`, `foreach (var kv in d)` z `kv.Key`/`kv.Value` (kolejność jak w .NET), inicjalizacja `{ {"a", 1} }` albo `{ ["a"] = 1 }`.
- **Korutyny (jak w Unity):** metoda zwracająca `IEnumerator` z `yield return null` (następna klatka), `yield return new WaitForSeconds(s)`, `yield return InnaKorutyna()` (czeka aż się skończy), `yield break`; `StartCoroutine(...)` (działa od razu do pierwszego `yield`), `StopCoroutine(c)`, `StopAllCoroutines()`. Wznawiane co klatkę po `Update()`, przed `Draw()`. Przykład: `examples/coroutines.cs`.
- **Tablice:** `new int[10]`, `new int[] { 1, 2, 3 }`, `.Length`, sprawdzanie zakresu.
- `using System;` jest akceptowane i ignorowane, więc kod z „prawdziwego" C# często wkleja się bez zmian.

### Punkty wejścia
- `static void Main()` – wywoływane raz na starcie.
- `static void Update()` i `static void Draw()` – wywoływane co klatkę (60 razy na sekundę). Jeśli jest `Update`, program działa jako gra.

### Wbudowane API

| Klasa | Metody / stałe |
|---|---|
| `Console` | `WriteLine(x)`, `WriteLine()`, `Write(x)` |
| `Math` | jak `System.Math`: `Sin Cos Tan Asin Acos Atan Atan2 Sqrt Pow Exp Log Log10 Floor Ceiling Round Truncate` (float), `Abs Min Max Clamp` (int i float), `Sign`, `PI`, `E` |
| `Mathf` | jak w Unity: wszystko z `Math` (ale `Ceil`) oraz `FloorToInt CeilToInt RoundToInt Clamp01 Lerp LerpUnclamped InverseLerp MoveTowards SmoothStep Repeat PingPong Approximately DeltaAngle Sign`, `Deg2Rad Rad2Deg Epsilon` |
| `Random` | `Next(max)`, `Range(min, max)`, `Float()`, `Seed(n)` |
| `Gfx` | `Screen(Screen.Top/Bottom)`, `Clear(kolor)`, `Rect(x,y,w,h,kolor)`, `Circle(x,y,r,kolor)`, `Line(x1,y1,x2,y2,kolor)`, `Text(x,y,tekst,kolor)`, `Text(x,y,tekst,skala,kolor)`, `Rgb(r,g,b)` |
| `Gfx` (obrazki) | `LoadImage("plik.png")` → numer (PNG/JPG/BMP, max 1024x1024, ścieżka względem folderu programu), `Image(img,x,y)`, `Image(img,x,y,skala)`, `ImageEx(img,x,y,skalaX,skalaY,kąt)` (ujemna skala = odbicie, obrót wokół środka), `ImagePart(img,x,y,sx,sy,sw,sh)` / `ImagePartEx(...,skalaX,skalaY,kąt)` (klatka ze sprite sheetu), `ImageWidth/ImageHeight(img)` |
| `Audio` | `Load("plik.wav")` → numer (WAV PCM 8/16 bit, mono/stereo), `Play(snd)`, `Play(snd,głośność)`, `PlayLoop(snd)` (muzyka), `Stop(snd)`, `StopAll()`; do 24 dźwięków naraz. Na 3DS potrzebny zrzut firmware DSP (DSP1); na PC dźwięk tylko się wczytuje |
| `Input` | `Held(Btn.X)`, `Pressed(Btn.X)`, `Touching()`, `TouchX()`, `TouchY()`, `StickX()`, `StickY()` |
| `Btn` | `A B X Y L R ZL ZR Up Down Left Right Select` (START jest zarezerwowany na powrót do menu) |
| `Color` | `Black White Red Green Blue Yellow Cyan Magenta Orange Gray DarkGray` |
| `Screen` | `Top`, `Bottom`, `TopWidth` (400), `BottomWidth` (320), `Height` (240) |
| `Time` | `Millis()`, `Fps()` |
| `App` | `Quit()` – kończy program i wraca do menu |

Kolory to zwykłe `int` w formacie `0xRRGGBB`.

### Czego jeszcze nie ma (pomysły na kolejne wersje)
- dziedziczenie, interfejsy, metody wirtualne,
- przeciążanie własnych metod, wiele konstruktorów,
- własne generyki, `long`, `out` poza `TryGetValue`,
- `enum`, `struct`, właściwości (`get`/`set`), interpolacja `$"..."`,
- edytor kodu na samej konsoli,
- backend generujący natywny kod ARM (statyczne typy już to umożliwiają).

## Plan

1. ~~Workflow w VS Code (IntelliSense, sprawdzanie, wysyłanie przez FTP, log z konsoli)~~
2. ~~Język: `foreach`, `switch`, `List<T>`, operatory bitowe, `char` i operacje na stringach~~ (test `tests/features2.cs` daje wynik identyczny z .NET)
   - ~~2b. `Mathf` w stylu Unity, korutyny, `Dictionary<K,V>`~~ (testy `tests/coroutines.cs` i `tests/dict.cs` dają to samo co prawdziwy .NET)
   - 2c. Debug przez Wi-Fi: VS Code wysyła kod do działającej aplikacji (bez zamykania, kompilacja i start od razu), `Console` i błędy na żywo w terminalu VS Code, później komendy (stop, restart, podgląd/zmiana zmiennych).
3. Podgląd na PC w oknie na żywo (sterowanie klawiaturą/myszą, ta sama czcionka co na 3DS).
4. ~~Sprite'y z PNG i dźwięk~~ (przykład `examples/sprites.cs`; Ctrl+Shift+B wysyła też obrazki i dźwięki leżące obok programu). Później: OGG/muzyka strumieniowa, sprite batching.
5. Programowanie grafami (w stylu Unity Bolt), na konsoli i na PC:
   - **graf → kod:** graf zapisuje się jako zwykły plik `.cs`, który kompiluje MiniSharp,
   - **kod → graf:** parser wczytuje `.cs` i pokazuje go jako graf; konstrukcje, których nie da się narysować, stają się węzłem „blok kodu”,
   - pozycje węzłów trzymane w komentarzach `// @node x,y`, więc plik dalej jest czystym C#,
   - **nowe węzły bez pisania C:** każda funkcja API to węzeł automatycznie, a każda metoda `static` w pliku `.cs` (np. biblioteka `nodes/*.cs`) staje się nowym węzłem.
6. Edytor MiniSharp na konsoli: pisanie kodu na dolnym ekranie (klawiatura dotykowa), autouzupełnianie z tabeli API (tej samej co paleta węzłów), podświetlanie błędów, uruchamianie jednym przyciskiem i przełączanie kod ↔ graf. Do tego terminal (REPL) na tej samej klawiaturze: górny ekran pokazuje wynik, np. `Math.Sqrt(2)` albo `Gfx.Circle(...)` od razu.
7. Backend generujący natywny kod ARM (opcjonalnie, jeśli zabraknie wydajności).

## Struktura

```
src/          rdzeń: lekser+parser, kompilator, VM, wbudowane API (wspólne dla PC i 3DS)
source3ds/    aplikacja na 3DS: menu, pętla gry, citro2d
pc/           wersja na PC do testów (rysuje do bufora, zapisuje .ppm)
examples/     przykłady (wgrywane na kartę SD przy pierwszym uruchomieniu)
tests/        testy z oczekiwanym wyjściem
tools/        gen_samples.py – osadza examples/*.cs w source3ds/samples.h
```

Po zmianie przykładów uruchom `python3 tools/gen_samples.py`.
