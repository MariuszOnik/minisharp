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

## W przeglądarce

**https://mariuszonik.github.io/minisharp/**: edytor i gra na jednej stronie, bez instalowania czegokolwiek. Działa też na telefonie.

- Kompilator i maszyna wirtualna to **ten sam kod w C** co na 3DS, skompilowany do WebAssembly (Zig, `wasm32-wasi`). Program, który działa w przeglądarce, działa też na konsoli.
- **Komputer:** po lewej kod (kilka plików `.cs` = projekt), po prawej oba ekrany w proporcjach 3DS. Sterowanie: strzałki = D-pad, `X`/`Z`/`S`/`A` = A/B/X/Y, `Q`/`W` = L/R, `Shift` = Select, `I J K L` = gałka, mysz na dolnym ekranie = dotyk, `Esc` = stop, `Ctrl+Enter` = uruchom.
- **Telefon:** zakładki *Kod* / *Gra*. W grze oba ekrany mają pełną szerokość i ten sam rozmiar (dolny jest rozciągnięty w poziomie), a pod nimi są przyciski: D-pad, A B X Y, L R, Select. Dotyk dolnego ekranu działa jak na konsoli.
- **Obrazki i dźwięki** dodajesz przyciskiem „+ obrazek / dźwięk” (PNG, JPG, BMP, WAV).
- **Udostępnij / Link** zapisuje pliki `.cs` w adresie strony (skompresowane), więc grę wysyłasz komuś jako link. Obrazki i dźwięki się do niego nie mieszczą.
- Kod zapisuje się sam w przeglądarce.

Strona buduje się i publikuje sama przy każdym pushu na `main` (GitHub Actions → GitHub Pages).
Lokalnie: `sh web/build.sh`, potem np. `python3 -m http.server -d web/dist` i otwórz `http://localhost:8000`.

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

### Projekty (wiele plików)
Projekt to folder: wszystkie pliki `.cs` w `sdmc:/minisharp/Nazwa/` kompilują się razem, klasy widzą się nawzajem, a obrazki i dźwięki są szukane w tym folderze. Plik „solucji” nie jest potrzebny. Program ma jeden punkt wejścia (`static void Main()` i/lub `Update()`/`Draw()` w dowolnej klasie; dwa `Main` to błąd).
- W menu projekty są na górze listy (na żółto, z `/` na końcu). Przykład: `Kosmos/` (`Game.cs`, `Ship.cs`, `Starfield.cs`).
- Błędy pokazują plik: `Ship.cs line 12: ...`.
- W VS Code projekt trzymaj w `games/Nazwa/` (z `namespace Nazwa;` w każdym pliku, żeby nazwy klas nie gryzły się z innymi grami). Ctrl+Shift+B sprawdza cały folder i wysyła go do `sdmc:/minisharp/Nazwa/`.
- Na PC: `./minisharp folder/`.

### Edytor
W menu **R** otwiera zaznaczony plik albo projekt w edytorze, **L** tworzy nowy projekt (folder z szablonem `Game.cs`).
- Kod z kolorami i numerami linii na górnym ekranie, klawiatura dotykowa na dolnym. Enter zachowuje wcięcie (po `{` dodaje poziom), `}` cofa wcięcie.
- Pasek: **Pliki** (otwórz / nowy plik / usuń w projekcie, też SELECT), **Uruchom** (zapisuje i uruchamia; przy błędzie kursor skacze do pliku i linii), **Cofnij** (też Y), **Help** (też X), **Zapisz**.
- Podpowiedzi nazw z API i z plików projektu (dotknij albo **R**); w nawiasie wywołania widać sygnaturę, np. `void Circle(float x, float y, float r, int color)`.
- **Help**: tematy o języku (`sdk/Help.txt`) i całe API z opisami (`sdk/MiniSharp.Api.cs`); A wstawia wybraną metodę do kodu. Po zmianie tych plików: `python3 tools/gen_help.py`.
- D-pad: kursor, L + D-pad: początek/koniec linii, strona w górę/dół. START: wyjście (pyta o zapis).

### Terminal (REPL)
W menu **SELECT** otwiera terminal: piszesz linię kodu na klawiaturze dotykowej, **A** (albo *Run*) ją wykonuje.
- Samo wyrażenie wypisuje wynik: `Math.Sqrt(2)`, `"abc".ToUpper()`. Średnik na końcu jest opcjonalny.
- Zmienne zostają na kolejne linie: `var x = 5`, potem `x * 10`. Działa też `for`, `if`, `List`, `Dictionary` itd.
- `Gfx.*` rysuje na płótnie na górnym ekranie, które nie czyści się między liniami (`Gfx.Clear` czyści). **X** przełącza płótno ↔ wyjście tekstowe.
- Pasek nad klawiaturą podpowiada nazwy (np. po `Gfx.` metody Gfx); dotknięcie albo **R** wstawia pierwszą.
- **B** kasuje, **←/→** przesuwa kursor (albo dotknij linii), **↑/↓** historia, **Y** czyści ekran, **START** wraca do menu.
- Pętla, która działa dłużej niż ok. 5 s, zostaje przerwana z błędem.
- Na PC: `./minisharp --repl` czyta linie ze standardowego wejścia (test `tests/repl.txt`).

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
- **Dziedziczenie i interfejsy:** `class Boss : Wrog, IRaniony`, `abstract class`, metody `virtual` / `override` / `abstract`, `base.Metoda()`, konstruktor `: base(...)`, `interface` (także `interface IB : IA`), `public override string ToString()` (używane przez `Console.WriteLine(obj)` i `"..." + obj`), `x is Boss`, `x is Boss b`, `x is null`, `x is not Boss`, `x as Boss`, rzutowanie `(Boss)x` (błąd wykonania, gdy obiekt nie jest Bossem). Kolejność konstruktorów i pól jak w .NET (test `tests/inherit.cs` daje identyczny wynik).
- **Instrukcje:** `if`/`else`, `while`, `do`/`while`, `for`, `foreach` (tablice, listy, stringi), `switch` (int, char, string, bool; `case 1: case 2:`, `default`), `break`, `continue`, `return`, bloki, `var`, `int a = 1, b = 2;`.
- **Wyrażenia:** `+ - * / %`, `== != < <= > >=`, `&& || !`, bitowe `& | ^ ~ << >>`, `? :`, `= += -= *= /= %= &= |= ^= <<= >>=`, `++ --` (przed i po), rzutowania `(int)` / `(float)` / `(char)` / `(Klasa)`, `is`, `as`.
- **Lambdy i delegaty:** `Func<...>`, `Action<...>`, `Predicate<T>`, `Comparison<T>`; `x => x * 2`, `(a, b) => a + b`, `(int x) => { ... }`, `() => ...`; nazwa metody jako delegat (`Func<int, int> f = Kwadrat;`, `Action a = obiekt.Metoda;`, także wirtualna); wywołanie `f(1)` i `f.Invoke(1)`; pola, tablice i listy delegatów; domknięcia jak w C# (lambda dzieli zmienne z metodą, `foreach` daje nową zmienną w każdym obrocie, `for` jedną). Test `tests/lambdas.cs` daje wynik identyczny z .NET.
- **Zdarzenia, `?.`, `??`:** `onSmierc += Metoda;`, `-=` (usuwa ostatnie wystąpienie, jak .NET), `public event Action x;`, `cel?.pole`, `cel?.Metoda()`, `onSmierc?.Invoke(this)`, `a ?? b`; przy `int`/`float` `?.` musi kończyć się `??` (`cel?.hp ?? 0`), bo nie ma `int?`. Metody z ciałem `=>` (`public int Pole() => w * h;`). Test `tests/listfunc.cs` identyczny z .NET.
- **Stringi:** łączenie `+` z dowolnym typem (`"x=" + x`), `.Length`, `s[i]` (char), porównanie `==`, `Substring IndexOf Contains StartsWith EndsWith ToUpper ToLower Trim Replace Split`, `string.IsNullOrEmpty`, `x.ToString()`.
- **char:** literały `'A'`, `'\n'`; `char.IsDigit IsLetter IsWhiteSpace ToUpper ToLower`; `c + 1` daje `int`, `c++` zostaje `char` (jak w C#). Znaki są 8-bitowe (ASCII), polskie litery w stringach liczą się jako kilka znaków.
- **Liczby:** `int.Parse(s)`, `float.Parse(s)`, `int.MaxValue`, `int.MinValue`.
- **List<T>:** `new List<int>()`, `new List<string> { "a", "b" }`, `new List<T>(innaListaLubTablica)`, `Add AddRange Insert Remove RemoveAt Clear Contains IndexOf Reverse Sort()`, z lambdą: `Find FindLast FindAll FindIndex FindLastIndex Exists TrueForAll RemoveAll ForEach Sort((a, b) => ...)`, `.Count`, `lista[i]`, `foreach`. `Sort` jest stabilny (.NET nie gwarantuje kolejności równych elementów); napisy sortują się jak w .NET z kulturą (`apple Apple banana`).
- **Dictionary<K,V>:** klucze `int`/`string`/`char`/obiekty, `d[k]` (też `d[k] += 1`), `Add Remove ContainsKey ContainsValue Clear`, `TryGetValue(k, out var v)`, `.Count .Keys .Values`, `foreach (var kv in d)` z `kv.Key`/`kv.Value` (kolejność jak w .NET), inicjalizacja `{ {"a", 1} }` albo `{ ["a"] = 1 }`.
- **Korutyny (jak w Unity):** metoda zwracająca `IEnumerator` z `yield return null` (następna klatka), `yield return new WaitForSeconds(s)`, `yield return InnaKorutyna()` (czeka aż się skończy), `yield break`; `StartCoroutine(...)` (działa od razu do pierwszego `yield`), `StopCoroutine(c)`, `StopAllCoroutines()`. Wznawiane co klatkę po `Update()`, przed `Draw()`. Przykład: `examples/coroutines.cs`.
- **Tablice:** `new int[10]`, `new int[] { 1, 2, 3 }`, `.Length`, sprawdzanie zakresu.
- `using System;` jest akceptowane i ignorowane, więc kod z „prawdziwego" C# często wkleja się bez zmian.

### Kurs dla początkujących
W Help (X w edytorze) tematy **Kurs 1** ... **Kurs 10** prowadzą od pierwszej zmiennej do małej gry: ruch i przyciski, pętle i listy, metody, klasy, dziedziczenie, interfejsy, lambdy i zdarzenia, korutyny, słownik. Cała gra z kursu to przykład [examples/kurs.cs](examples/kurs.cs) (statek, fale nietoperzy i kamieni). Dalej tematy **Loch 1** ... **Loch 4** objaśniają przykład [examples/loch.cs](examples/loch.cs): roguelike z liter z losowym lochem, walką turową, systemem komponentów jak w Unity (`Byt` + `Komponent`) i maszyną stanów (FSM) potworów. Przykłady z rozdziałów 1-4 sprawdza test `tests/kurs_snippets.cs`.

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
- mini Linq (`Where`, `Select`, `OrderBy`...), `int?`, właściwości `get`/`set`, inicjalizatory obiektów `new X { a = 1 }`,
- `switch` na typach (`case Boss b:`), ukrywanie metod (`new`), `protected` sprawdzane przez kompilator,
- przeciążanie własnych metod, wiele konstruktorów,
- własne generyki, `long`, `out` poza `TryGetValue`,
- `enum`, `struct`, właściwości (`get`/`set`), interpolacja `$"..."`,
- edytor kodu na samej konsoli,
- backend generujący natywny kod ARM (statyczne typy już to umożliwiają).

## Plan

1. ~~Workflow w VS Code (IntelliSense, sprawdzanie, wysyłanie przez FTP, log z konsoli)~~
2. ~~Język: `foreach`, `switch`, `List<T>`, operatory bitowe, `char` i operacje na stringach~~ (test `tests/features2.cs` daje wynik identyczny z .NET)
   - ~~2b. `Mathf` w stylu Unity, korutyny, `Dictionary<K,V>`~~ (testy `tests/coroutines.cs` i `tests/dict.cs` dają to samo co prawdziwy .NET)
   - 2d. Język, wersja 3 (po kolei): ~~dziedziczenie i interfejsy~~, ~~lambdy, `Func`/`Action`, domknięcia~~, ~~`?.`, `??` i `+=` na delegatach (zdarzenia), metody `List` z predykatami~~; dalej mini Linq (zachłanny: `Where Select OrderBy OrderByDescending First FirstOrDefault Any All Count Sum Min Max ToList`); mniejsze: `enum`, właściwości `get`/`set`, interpolacja `$"..."`, `switch` na typach (`case Boss b:`), przeciążanie metod i wiele konstruktorów.
   - 2c. Debug przez Wi-Fi: VS Code wysyła kod do działającej aplikacji (bez zamykania, kompilacja i start od razu), `Console` i błędy na żywo w terminalu VS Code, później komendy (stop, restart, podgląd/zmiana zmiennych).
3. Podgląd na PC w oknie na żywo (sterowanie klawiaturą/myszą, ta sama czcionka co na 3DS).
4. ~~Sprite'y z PNG i dźwięk~~ (przykład `examples/sprites.cs`; Ctrl+Shift+B wysyła też obrazki i dźwięki leżące obok programu). Później: OGG/muzyka strumieniowa, sprite batching.
5. Programowanie grafami (w stylu Unity Bolt), na konsoli i na PC:
   - **graf → kod:** graf zapisuje się jako zwykły plik `.cs`, który kompiluje MiniSharp,
   - **kod → graf:** parser wczytuje `.cs` i pokazuje go jako graf; konstrukcje, których nie da się narysować, stają się węzłem „blok kodu”,
   - pozycje węzłów trzymane w komentarzach `// @node x,y`, więc plik dalej jest czystym C#,
   - notatki projektowe (model portów, katalog węzłów): [docs/graphs-notes.md](docs/graphs-notes.md),
   - **nowe węzły bez pisania C:** każda funkcja API to węzeł automatycznie, a każda metoda `static` w pliku `.cs` (np. biblioteka `nodes/*.cs`) staje się nowym węzłem.
6. ~~Terminal (REPL) z klawiaturą dotykową i podpowiedziami~~, ~~projekty z wielu plików (folder = projekt)~~. Dalej edytor MiniSharp na konsoli, który umie też tworzyć projekty (nowy folder z szablonem `Game.cs`), dodawać i usuwać w nich pliki; pisanie kodu na dolnym ekranie (klawiatura dotykowa), autouzupełnianie z tabeli API (tej samej co paleta węzłów), podświetlanie błędów, uruchamianie jednym przyciskiem i przełączanie kod ↔ graf. Do tego terminal (REPL) na tej samej klawiaturze: górny ekran pokazuje wynik, np. `Math.Sqrt(2)` albo `Gfx.Circle(...)` od razu.
7. Backend generujący natywny kod ARM (opcjonalnie, jeśli zabraknie wydajności).

## Struktura

```
src/          rdzeń: lekser+parser, kompilator, VM, wbudowane API (wspólne dla PC i 3DS)
source3ds/    aplikacja na 3DS: menu, pętla gry, citro2d
pc/           wersja na PC do testów (rysuje do bufora, zapisuje .ppm)
web/          wersja w przeglądarce: WebAssembly + strona (edytor, ekrany, przyciski dotykowe)
examples/     przykłady (wgrywane na kartę SD przy pierwszym uruchomieniu)
tests/        testy z oczekiwanym wyjściem
tools/        gen_samples.py – osadza examples/*.cs w source3ds/samples.h
```

Po zmianie przykładów uruchom `python3 tools/gen_samples.py`.
