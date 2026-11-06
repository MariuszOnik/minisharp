# Programowanie grafami – notatki do punktu 5 planu

Notatki z przeglądu Bolta 1.4 (Unity, Ludiq), który służy tu tylko jako punkt odniesienia.
**Nie kopiujemy z niego kodu ani grafik** (licencja Asset Store; to repo jest publiczne).
Edytor grafów MiniSharp piszemy od zera.

## Różnica w podejściu

- Bolt **interpretuje** graf w czasie gry (refleksja .NET, obiekt `Flow` przechodzi po węzłach).
- MiniSharp **zamienia graf na zwykły plik `.cs`**, który kompiluje nasz kompilator do bajtkodu.
  Na 3DS to dużo szybsze, a plik dalej jest czystym C# (pozycje węzłów w komentarzach `// @node x,y`).

## Model węzła (pomysł do przejęcia)

Każdy węzeł ma dwa rodzaje portów:

| Port | Znaczenie | W kodzie |
|---|---|---|
| przepływu (wejście `enter`, wyjścia `exit`, `body`, `true`/`false`...) | kolejność wykonania | instrukcje |
| wartości (wejścia z wartością domyślną, wyjścia) | dane | wyrażenia |

Węzeł deklaruje zależności:
- *wymaganie* – które wartości są potrzebne, zanim wejdzie przepływ,
- *przypisanie* – które wyjścia wartości ustawia, gdy przez niego przejdzie przepływ,
- *następstwo* – z którego wejścia przepływu można wyjść którym wyjściem.

To dokładnie informacja potrzebna generatorowi kodu: porty przepływu wyznaczają kolejność instrukcji,
a porty wartości budują drzewo wyrażenia (wartość bez przepływu = wyrażenie liczone w miejscu użycia;
wyjście ustawiane przez przepływ, np. indeks pętli = zmienna lokalna).

Przykład – pętla For: wejścia wartości First, Last, Step; wyjście wartości Index; przepływ enter → body (powtarzane) → exit.
Kod: `for (int i = first; i < last; i += step) { body }`, potem to, co podłączone do exit.

## Katalog węzłów (kategorie jak w Bolcie → odpowiednik w MiniSharp)

| Kategoria | Węzły | MiniSharp |
|---|---|---|
| Control | Branch (if), For, ForEach, While, Sequence, Switch on int/string, Once, Toggle, Break | instrukcje; Once/Toggle = pole `bool` |
| Logic | And, Or, Not, Xor, Equal, Less, Greater..., Approximately | operatory, `Mathf.Approximately` |
| Math | Add, Subtract, Multiply, Divide, Modulo, Min, Max, Abs, Round, Lerp, MoveTowards, Average, Sum, Per Second | operatory, `Math`/`Mathf` |
| Collections | List: create, add, insert, remove, get, contains, count, first/last; Dictionary: create, add, get, set, remove, contains key | `List<T>`, `Dictionary<K,V>` |
| Nulls | Null, Null Check (dwa wyjścia przepływu), Null Coalesce | `== null`, `??` (do zrobienia) |
| Time | Wait (sekundy), Wait Until/While, Next Frame, Cooldown, Timer | korutyny: `yield return new WaitForSeconds`, `yield return null` |
| Events | On Update, On Start, Custom Event + Trigger | `Update()`, `Main()`, pola `Action` (+ `+=`, `?.Invoke()`) |
| Variables | zmienne grafu / obiektu / globalne (get, set, is defined) | pola klasy, pola `static` |
| Codebase | dowolna metoda / pole / właściwość | każda funkcja API i każda metoda `static` z `nodes/*.cs` jako węzeł |
| Nesting | podgraf z wejściami i wyjściami | metoda z parametrami i wynikiem |

Wnioski:
- Prawie każdy węzeł Bolta ma już odpowiednik w języku MiniSharp – graf będzie cienką warstwą nad kodem.
- Wait/Cooldown jako korutyny wymagają, żeby graf zdarzenia mógł się stać metodą `IEnumerator`.
- Custom Event to pole `Action` – dlatego warto najpierw dodać `+=` i `?.Invoke()` (punkt 2d).
- Maszyny stanów (`Bolt.State`: stany, przejścia, warunki) – pomysł na później, np. AI wrogów.

## Do zaprojektowania przy punkcie 5

1. Format zapisu: jak węzły i połączenia mapują się na kod i komentarze `// @node`.
2. Reguły generowania kodu dla portów (kiedy wyjście wartości staje się zmienną lokalną).
3. Kod → graf: które konstrukcje rysujemy, a które idą do węzła „blok kodu”.
4. Interfejs na 3DS: dotyk na dolnym ekranie, szukanie węzła po nazwie (jak „fuzzy finder” w Bolcie), przewijanie i zoom.

## Decyzje (kroki 1–2, `web/graph.js`)

**Format grafu** (zwykły JSON, `MSGraph.sampleBall()` jest przykładem):
`{ className, variables: [{name, type, init}], nodes: [{id, type, x, y, data}], edges: [{from, out, to, in}] }`.
`data` ma `name` (zmienne), `value` (węzeł „Wartość”) i `defaults: { port: "wyrażenie C#" }` dla wejść wartości bez połączenia.
Zmienne grafu stają się polami `static`, bo `Update` i `Draw` są metodami `static`.

**Zapis w pliku:** wygenerowany `.cs` kończy się jedną linią `// @graph {json}` (to zwykły komentarz, plik dalej jest czystym C#).
To zastępuje na razie osobne komentarze `// @node x,y` i parser kod → graf: graf wraca bez strat z tej jednej linii.
Parser kod → graf (punkt 3 poniżej) jest nadal do zrobienia.

**Reguły generowania (punkt 2):**
- porty przepływu dają kolejność instrukcji, porty wartości budują wyrażenia; nawiasy tylko tam, gdzie wymaga ich pierwszeństwo operatorów,
- węzeł z wartością, do którego nie wchodzi przepływ, liczy się w miejscu użycia (każde użycie osobno),
- węzeł, przez który przechodzi przepływ, i którego wynik jest używany, daje zmienną lokalną (`var loadImage = Gfx.LoadImage(...)`), a wynik nieużywany nie jest zapamiętywany,
- indeks `For` to zmienna pętli (`i`, `j`, `k`, ...), ważna tylko w ciele pętli; użycie lokalnej poza blokiem, w którym powstała, daje ostrzeżenie,
- `x = x - 3` zapisuje się jako `x -= 3`,
- `If` ma wyjścia `true`, `false` i `exit` (dalszy ciąg po bloku); dwa przepływy schodzące się w tym samym węźle powielają jego kod w obu gałęziach,
- kilka węzłów tego samego zdarzenia (np. dwa `Update`) uruchamia się po kolei, od górnego,
- każda funkcja API to węzeł, wejścia bez połączenia mają wartość domyślną z typu (`0`, `false`, `""`) albo wpisaną w `defaults`; przeciążenia o różnej liczbie parametrów to osobne węzły (`Gfx.Text (4)`, `Gfx.Text (5)`),
- pętla w przepływie albo cykl w wartościach nie zawiesza generatora, tylko daje ostrzeżenie lub błąd.

Testy (`tests/web/graph.test.js`, w CI po testach PC): obraz ostatniej klatki z grafu jest identyczny bajt w bajt z obrazem programu pisanego ręcznie
(kulka sterowana przyciskami, zagnieżdżone pętle), a wygenerowany kod przechodzi przez prawdziwy kompilator.

## Edytor w przeglądarce (`web/grapheditor.js`)

- **Karta „Graf”** obok „Kod” i „Gra” (na komputerze: „Kod” i „Graf” obok gry). Ma tylko przybornik i płótno, bez paska plików i obrazków, żeby na telefonie
  było miejsce na węzły. Przybornik: nazwa pliku z grafem (▾ lista plików z grafem i „+ Nowy graf”), „+ Węzeł”, „Usuń”, „⋯” (zmienne grafu, dopasuj widok,
  uwagi, pokaż wygenerowany kod, zamień na zwykły kod) i znaczek ⚠ z liczbą uwag generatora, gdy są. Przycisk ⤢ na płótnie dopasowuje widok.
- Plik `.cs` z linią `// @graph` jest plikiem z grafem (w pasku plików ma znaczek ◇). Jego kod powstaje z grafu przy każdej zmianie, więc na karcie „Kod”
  jest tylko do czytania; „Zamień na zwykły kod” odpina graf i plik staje się zwykłym, edytowalnym kodem. Karta „Graf” bez grafu w projekcie pokazuje
  zaproszenie „+ Nowy graf”; „+ dodaj” → „Plik z grafem” robi to samo. Nowy graf zaczyna od węzłów Update i Draw (każdy graf może mieć własne, hooki).
- Każda zmiana grafu od razu generuje kod do pliku, więc „Uruchom” działa jak zawsze; błąd kompilacji przenosi na kartę „Kod” w miejsce błędu.
- Dotyk i mysz: przeciągnięcie tła przesuwa widok, dwa palce albo kółko myszy powiększają, węzeł przesuwa się za pasek tytułu (siatka co 10),
  porty łączy się przeciągnięciem albo dotknięciem pierwszego i drugiego, dotknięcie przewodu lub węzła zaznacza go („Usuń”),
  dotknięcie wartości w węźle (domyślna wartość wejścia, liczba w „Wartość”, nazwa zmiennej) otwiera jej edycję. Tekst wpisany do wejścia typu
  `string` albo `object` dostaje cudzysłów sam (liczba, `true`/`false`, nazwa zmiennej i wywołanie zostają bez zmian).
- Paleta „+ Węzeł” szuka po nazwie, kategorii i opisie. Nowy węzeł ląduje po prawej stronie zaznaczonego (łańcuchy rosną od lewej do prawej),
  a bez zaznaczenia w najbliższym wolnym miejscu pośrodku widoku.
- Jeszcze nie ma: cofania, kopiowania węzłów, automatycznego układania ani kod → graf.

## Własne węzły z kodu

Każda metoda `static` w pliku z kodem (nie w pliku z grafem) jest węzłem; rejestrować nic nie trzeba. Węzeł pojawia się w palecie „+ Węzeł” na samej górze,
w kategorii „Kod: NazwaKlasy”, a po edycji kodu odświeża się przy następnym otwarciu palety i przy wejściu na kartę „Graf”.
- Pomijane: `Main`, `Update`, `Draw` (to wejścia programu), metody bez `static` i metody z komentarzem `// @node hide`.
- Opis węzła to linie `//` albo `///` tuż nad metodą; `// @node Nowa nazwa` zmienia tytuł węzła (nazwa metody zostaje w wywołaniu).
- Węzeł ma przepływ (`enter`/`exit`), wejście wartości na każdy parametr (z wartością domyślną z typu) i wynik (`result`), jeśli metoda coś zwraca.
  Parametr o nazwie `enter`, `exit`, `result` albo `value` dostaje w porcie dopisane `_`.
- Wygenerowany kod woła `Klasa.Metoda(argumenty)`. Gdy metoda zniknie z kodu, węzeł w grafie zostaje (czerwony, „? user.Klasa.Metoda”),
  a generator zgłasza „nieznany typ węzła”, więc nic nie ginie po cichu.

## Graf razem z kodem: hooki, metody i obiekty

Wszystkie pliki `.cs` projektu kompilują się razem, więc klasa z grafu jest widoczna w kodzie i odwrotnie (własne węzły z kodu: sekcja wyżej).

**Hooki (jak skrypty na scenie w Unity).** Każda klasa, także wygenerowana z grafu, może mieć własne `static void Main()`, `Update()` i `Draw()`.
Kompilator robi z nich jedno wejście: woła wszystkie, w kolejności klas w projekcie (pliki po nazwie, klasy od góry), więc `Game.cs` i graf mają
własne `Update` i nie przeszkadzają sobie. Późniejszy plik rysuje na wierzchu. Metoda z parametrem albo bez `static` nie jest hookiem.
Błąd w hooku pokazuje plik i linię (`B.cs line 4 (B.Update)`). To zmiana w kompilatorze (`entry_point` w `src/compiler.c`), więc działa
tak samo na PC, w przeglądarce i na 3DS; hosty nadal wołają jedno wejście.

**Węzeł „Metoda (wołana z kodu)”** (nazwę zmieniasz dotknięciem w węźle): łańcuch staje się metodą klasy grafu bez parametrów, którą wołasz sam,
np. `Graph.Tick();` z kodu albo z innego grafu. Nad metodą generator wpisuje podpowiedź „Do wywołania z kodu: ...”. Zmienne grafu to pola tej klasy
(`Graph.x`), więc kod może je czytać i ustawiać.

**Dwa tryby klasy** (menu `⋯` na karcie „Graf”):
- *statyczna* (domyślnie): pola i metody `static`, wołasz `Graph.Tick()`; zdarzenia Start, Update i Draw to hooki,
- *z obiektami* (`"instance": true` w grafie): klasa bez `static`, szablon do `var o = new Graph(); o.Tick();`, a każdy obiekt ma własne zmienne
  (test: dwie piłki z jednej klasy dają ten sam obraz co ręcznie napisana klasa). Start, Update i Draw są statyczne, więc w takiej klasie nie działają
  i generator zgłasza błąd; zostają węzły „Metoda”.

Uwagi spoza grafu (edytor sprawdza pozostałe pliki projektu): projekt bez `Main` i `Update`.

**Zmienne sceny** (wspólne dla wszystkich grafów) są pomysłem na następny krok: osobna, generowana klasa `Scene` z polami `static`, do której węzły
„Pobierz/Ustaw zmienną” odwołują się przez `Scene.nazwa`, obok zmiennych grafu (pola jego klasy).

## Kierunek i plan (ustalenia z rozmowy)

**Rdzeń:** graf generuje zwykły kod. Kompilator, wasm i 3DS nie znają grafu, więc wszystko, co działa w kodzie, działa też z grafu.
Poprawność sprawdza jeden rodzaj testu: obraz z grafu jest identyczny z obrazem programu pisanego ręcznie (`tests/web/graph.test.js`, prawdziwy kompilator).
Graf nie dostaje własnego środowiska uruchomieniowego.

**Graf jako komponent (jak skrypt na scenie w Unity):** dzięki hookom ma własne `Update` i `Draw`, własne zmienne (pola swojej klasy), woła inne klasy i własne
węzły z kodu; w trybie z obiektami jest szablonem do `new`. Brakuje przypinania obiektów do sceny (rejestracja obiektu w silniku, żeby dostawał `Update`).

**Kod ↔ graf:** nie na żywo w obie strony (graf jest przy tym stratny: formatowanie, komentarze, `switch`, zagnieżdżone lambdy, długie wyrażenia).
Eksport (graf → kod) jest. Import (kod → graf) ma zawsze się udawać: co parser rozpozna (`if`, `for`, wywołania, operatory, zmienne), staje się węzłami,
reszta trafia do węzła **„Surowy kod”** z dosłownym tekstem, który generator wypisze bez zmian. Parser najlepiej w C (zna cały język; wyjście z drzewem
składni dla przeglądarki, ten sam parser pasuje potem do 3DS). Test spójności: graf → kod → graf daje ten sam graf (bez układu węzłów).

**Komunikacja między grafami i kodem:** najpierw *zmienne sceny* (stan: wynik, życie): jedna generowana klasa `Scene` z polami `static`, węzły zmiennych
odwołują się do niej przez `Scene.nazwa`, zmienne grafu zostają jego prywatnymi polami. *Zdarzenia* (wiadomość „gracz zginął”) jako cienka nakładka
(klasa z polami `Action`, węzły „Wyślij zdarzenie” i „Na zdarzenie”) dopiero wtedy, gdy gra ich zażąda.

**Graf jako węzeł (podgraf):**
1. Metody grafu (węzły „Metoda”) mają być widoczne jako węzły w innych grafach. Dziś `userNodes` w `web/graph.js` pomija pliki z grafem, bo zakładał, że mają w klasie
   tylko zdarzenia; wystarczy pomijać same Main/Update/Draw (już pomijane) i nie pomijać plików z grafem. Węzeł byłby na razie bez parametrów i wyniku.
2. Parametry i wynik: dynamiczne porty węzła „Metoda” i węzeł „Zwróć” (funkcja `Dystans(a, b) → liczba`). Większa zmiana: katalog węzłów ma dziś porty statyczne.

**Kolejność:** (1) gra-próbka na telefonie, (2) metoda grafu jako węzeł, (3) zmienne sceny, (4) import z „Surowym kodem”, (5) zdarzenia, jeśli gra ich potrzebuje.
Cofanie i automatyczne układanie węzłów dopiero, gdy gra-próbka pokaże, że ręczne przesuwanie węzłów palcem męczy. Na później: parametry metod, biblioteka `nodes/*.cs`,
edytor grafu na 3DS (plik z grafem to zwykły C#, więc na konsoli uruchamia się już dziś), korutyny (Wait) w grafie.

## Gra-próbka (do złożenia na telefonie, jedna sesja)

Cel: sprawdzić w użyciu to, co jest zbudowane, i zapisać, co zawiodło. Nic nowego nie dodajemy, dopóki gra tego nie zażąda.

1. Nowy projekt; zakładka „Graf” → „+ Nowy graf” → `Gracz.cs`. Zmienne `x`, `y` (float, 200 i 120). `Update`: cztery razy „Jeśli” + `Input.Held(Btn.…)` →
   „Ustaw zmienną” (`x`/`y` ± 3). `Draw`: `Gfx.Clear` i `Gfx.Circle` z `x`, `y`.
2. Plik z kodem `Lib.cs` z metodą `static bool Dotyka(float ax, float ay, float bx, float by, float r)` (odległość mniejsza niż `r`). Sprawdź, że pojawia się w palecie
   w kategorii „Kod: Lib” i że komentarz nad metodą jest jej opisem.
3. Drugi graf `Moneta.cs` w trybie z obiektami (menu `⋯`): zmienne `x`, `y`, metody „Metoda” `Rysuj` i `Sprawdz`. W kodzie (`Game.cs`): lista monet, `new Moneta()`,
   wołanie `Rysuj` i `Sprawdz`. Sprawdź, że każda moneta ma własne `x`, `y`.
4. Wynik: zmienna `score` w grafie `Gracz` i `Gfx.Text` z jej wartością. Sprawdź, jak moneta (inny plik) zwiększa wynik gracza: dziś tylko przez kod (`Gracz.score`),
   to jest dokładnie miejsce, które pokaże, czy potrzebne są zmienne sceny.
5. Uruchom na telefonie. Zapisz: co było niejasne, co trzeba było robić kodem zamiast węzłami, gdzie palec nie trafia, czego brakowało w palecie.

Z tej listy wybieramy najwyżej trzy poprawki; reszta idzie do „Na później”.
