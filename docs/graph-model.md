# Model grafu (punkt 5 planu)

Dokument projektowy rdzenia `src/graph.c`. Ustalenia, na których stoi (z rozmowy 2026-11-06):

- **źródłem prawdy jest kod `.cs`**, graf jest *widokiem* drzewa składni (AST), które już buduje `src/parser.c`,
- każdy poprawny program MiniSharp da się pokazać jako graf, a każda zmiana w grafie daje poprawny kod,
- **podgraf = metoda `static`** (parametry = wejścia, `return` = wyjście),
- **3DS ma pierwszeństwo**; ten sam rdzeń w C trafi potem do przeglądarki (wasm).

Starsze notatki (Bolt, edytor w przeglądarce, format `// @graph {json}`) są w [graphs-notes.md](graphs-notes.md).
Ten dokument je zastępuje tam, gdzie się różnią.

## 1. Najważniejsza zasada: każda edycja grafu jest edycją tekstu

```
 tekst .cs ──parse──▶ AST ──build──▶ graf (widok) ──rysuj──▶ ekran
     ▲                                   │
     └──── łatka tekstu (zakres → nowy tekst) ◀── akcja w edytorze grafu
```

Graf nie ma własnego stanu poza układem węzłów. Akcja w edytorze (zmień liczbę, wstaw wywołanie, usuń instrukcję)
zamienia się w **łatkę tekstu**: „zastąp bajty `[od, do)` tym tekstem”. Potem plik jest parsowany od nowa i graf budowany od nowa.

Dlaczego tak:
- kod i graf nie mogą się rozjechać, bo istnieje tylko kod,
- komentarze, formatowanie i wszystko, czego graf nie pokazuje, zostaje nietknięte (łatka zmienia tylko swój zakres),
- kompilator, VM i 3DS nie wiedzą nic o grafie,
- cofanie (undo) to stos łatek tekstu, taki sam jak w edytorze kodu.

Koszt: pełne parsowanie po każdej akcji. Plik 1800 linii (`Game.cs`) trzeba zmierzyć na konsoli. Akcja w grafie zdarza się raz na dotknięcie,
a nie co klatkę, więc nawet 20–30 ms jest do przyjęcia. Gdyby było za wolno, parsuje się tylko zmienioną metodę.

### Czego brakuje dziś w AST

| Brak | Co trzeba dodać |
|---|---|
| zakres w tekście (jest tylko `line`) | `const char *s0, *s1` w `Expr`, `Stmt`, `MethodDecl`, `FieldDecl`, `ClassDecl` (ustawiane z `Token.start`/`len`) |
| ciało `=> wyrażenie` zamienia się w blok z jednym `return` | flaga `exprBody` w `MethodDecl`, żeby wiedzieć, że dopisanie drugiej instrukcji wymaga zamiany na `{ ... }` |
| modyfikatory (`public`, `readonly`, `event`) parser pomija | nic: łatka nie dotyka nagłówka metody, więc zostają w tekście |
| komentarze lekser pomija | nic dla poprawności; opcjonalnie: komentarz `//` tuż nad instrukcją jako notatka na węźle (szukany w tekście nad `s0`) |

Dwa wskaźniki na każdy węzeł AST to kilka kB nawet dla dużych plików, więc pamięć nie jest problemem.

## 2. Co jest węzłem

### Trzy poziomy: projekt → klasa → metoda

1. **Projekt:** karty klas pogrupowane według plików. Plik nie jest grafem, tylko pudełkiem na klasy.
2. **Klasa:** karta jak komponent w inspektorze Unity, czyli pola (typ, wartość początkowa) i metody. Hooki `Main`/`Update`/`Draw` są oznaczone jako zdarzenia.
   Kartę klasy da się edytować (zmiana wartości początkowej pola to łatka tekstu), ale nie ma na niej przepływu.
3. **Metoda:** właściwy graf z przepływem i wartościami. Konstruktor też jest metodą.

**Obiekt** nie jest osobnym węzłem z danymi: dane istnieją dopiero w czasie gry, a graf pokazuje kod. W grafie obiekt jest *wartością*,
np. pole `Game.ship` typu `Ship`. Z jej wyjścia ciągnie się przewód do węzła wywołania (wejście „obiekt”), a paleta podpowiada pola i metody
klasy `Ship` (te dane zna już `src/codeindex.c`). Każde wywołanie metody z projektu (statycznej albo nie) ma przycisk „wejdź”, który otwiera jej graf.
Metoda `static` z parametrami i wynikiem jest najczystszą formą podgrafu (funkcja: wejścia → wyjście).

**Jeden graf = jedna metoda.**

### Węzły przepływu = instrukcje

| AST | Węzeł | Porty przepływu | Porty wartości |
|---|---|---|---|
| `S_EXPR` z `E_CALL` | wywołanie | wejście, dalej | argumenty; wynik nieużywany |
| `S_EXPR` z `E_ASSIGN` / `E_INCDEC` | ustaw (`x = …`, `x += …`, `x++`) | wejście, dalej | cel (tekst), wartość |
| `S_VAR` | nowa zmienna | wejście, dalej | wartość początkowa |
| `S_IF` | jeśli | wejście, *tak*, *nie*, dalej | warunek |
| `S_WHILE`, `S_DO` | dopóki | wejście, *ciało*, dalej | warunek |
| `S_FOR` | pętla for | wejście, *ciało*, dalej | init / warunek / krok (zwykle jako tekst) |
| `S_FOREACH` | dla każdego | wejście, *ciało*, dalej | kolekcja; zmienna pętli |
| `S_SWITCH` | wybór | wejście, jedno wyjście na `case`, dalej | wartość |
| `S_RETURN`, `S_YIELD` | zwróć / czekaj | wejście | wartość |
| `S_BREAK`, `S_CONTINUE`, `S_YIELDBREAK` | przerwij / dalej / koniec | wejście | — |
| `S_BLOCK` | nie ma własnego węzła; jego instrukcje tworzą łańcuch | | |

**Przepływ jest drzewem, a nie dowolnym grafem.** Kolejne instrukcje bloku to łańcuch, a *tak*/*nie*/*ciało* otwierają łańcuchy zagnieżdżone.
Dwa przewody nie mogą wchodzić do jednego węzła (w przeglądarce generator powielał wtedy kod, tutaj takiego stanu po prostu nie da się zbudować).
Dzięki temu graf ↔ kod jest 1:1, bez zgadywania.

### Węzły wartości = wyrażenia

Wyrażenie to drzewo, więc węzły wartości też tworzą drzewo: każde wyjście wartości idzie do dokładnie jednego wejścia.
Wartość użyta dwa razy to w kodzie zmienna lokalna, a w grafie węzeł „nowa zmienna” plus dwa węzły „pobierz”.

**Zwijanie.** Nie każde wyrażenie musi być osobnym węzłem. Wejście pokazuje wyrażenie jako tekst, jeśli jest *małe*:
literał, nazwa, `a.b`, lambda, albo tekst w kodzie ma najwyżej 24 znaki i jedną linię (`x + 1`, `Input.Held(Btn.Right)`). Większe rozwija się w węzły.
Użytkownik może rozwinąć albo zwinąć dowolne wejście; ten wybór zapisuje się w układzie (punkt 4), bo nie zmienia kodu.
Tekst w wejściu edytuje się klawiaturą. To jest zwykła łatka zakresu tego wyrażenia; jeśli się nie sparsuje, łatka nie wchodzi i edytor pokazuje błąd.

Lambdy (`E_LAMBDA`) na początek zawsze jako tekst; rozwinięcie w zagnieżdżone płótno dopiero później.

### Bez węzła „Surowy kod”

Ponieważ każdy węzeł ma zakres w tekście, nie potrzeba osobnego węzła „surowy kod”: wszystko, czego graf nie umie rozłożyć, jest po prostu
zwiniętym tekstem w węźle albo wejściu. Nic nie ginie, bo tekst zostaje w pliku.

## 3. Wstawianie nowego kodu (drukarka)

Łatki z nowym kodem (wstaw węzeł, rozwiń w blok) potrzebują **drukarki AST → tekst** z jednym stałym stylem (wcięcie 4 spacje, nawiasy
tylko tam, gdzie wymaga ich kolejność operatorów, `x -= 3` zamiast `x = x - 3`). Reguły nawiasów i skrótów można przenieść z `web/graph.js`.
Drukarka służy tylko do nowego kodu; istniejący tekst nigdy nie jest drukowany od nowa.

Przykłady łatek:
- *zmień wartość wejścia*: zastąp zakres wyrażenia nowym tekstem,
- *wstaw instrukcję po węźle*: wstaw `\n` + wcięcie + tekst za końcem zakresu instrukcji (wcięcie odczytane z linii tej instrukcji),
- *usuń węzeł*: usuń zakres instrukcji razem z całą jej linią, jeśli nic innego w niej nie ma,
- *wstaw do pustego `if`*: zamień `if (c) x();` na `if (c) { x(); y(); }`, gdy ciało nie jest blokiem,
- *podłącz wartość*: zastąp tekst wejścia tekstem wyrażenia; przesunięcie węzła wartości z innego miejsca to dwie łatki (wytnij, wstaw).

## 4. Układ węzłów

Pozycje nie należą do kodu, ale muszą przetrwać zamknięcie edytora. Zapis: **jeden komentarz na końcu pliku**, poza klasami:

```csharp
/* @layout
Gracz.Update e:entry:0,0 0:if:12,4 0.t0:set:30,60 0.t0.a0:val:-60,60 0.t0.a0=open
Gracz.Draw e:entry:0,0 0:call:12,4
*/
```

- wpis węzła = `klucz:rodzaj:x,y`; klucz to ścieżka w drzewie metody: numer instrukcji, `.t`/`.f`/`.b`/`.cN_` dla gałęzi tak/nie/ciało/case, `.aN` dla wejścia wartości
  (`e` to węzeł wejścia metody); rodzaj (`if`, `call`, `set`, `var`, `for`, `val`, ...) pozwala zauważyć, że pod tym kluczem jest już inna instrukcja,
- `=open` / `=closed` zapisuje ręczne rozwinięcie lub zwinięcie wejścia,
- edytor kodu na 3DS może ten blok zwijać, żeby nie przeszkadzał.

Po ręcznej zmianie kodu ścieżki mogą wskazywać złe węzły. Zasada: przy budowaniu grafu węzeł z kluczem, który nie pasuje (inny rodzaj instrukcji),
dostaje pozycję z **automatycznego układu**, a nie starą. Węzeł wartości bez własnego wpisu przesuwa się razem z węzłem, który zasila. Automatyczny układ jest też dla węzłów bez wpisu, więc plik bez `@layout` (każdy dzisiejszy program)
od razu ma czytelny graf. Edytor grafu przepisuje blok `@layout` przy każdej swojej łatce, dzięki czemu ścieżki zgadzają się z kodem.

**Automatyczny układ** (proponowany, do sprawdzenia na prototypie): przepływ idzie **z góry na dół** (łańcuch instrukcji w kolumnie,
gałęzie *tak*/*ciało* przesunięte w prawo, jak wcięcia w kodzie), a węzły wartości stoją **na lewo** od wejścia, które zasilają.
Taki układ jest deterministyczny, liczy się w jednym przejściu po drzewie i dobrze pasuje do wąskiego, wysokiego przewijania na 320×240.
Alternatywa w stylu Unity (przepływ od lewej do prawej) zostaje do porównania na konsoli.

## 5. Struktury w C

```c
typedef struct GNode {
    int kind;                 /* GN_CALL, GN_SET, GN_VAR, GN_IF, ... GN_VALUE, GN_TEXT */
    Stmt* st; Expr* ex;       /* dokładnie jedno z nich */
    int parent, port;         /* do którego węzła i portu jest podłączony (drzewo) */
    int first, count;         /* dzieci w tablicy portów */
    float x, y, w, h;         /* układ */
    unsigned flags;           /* open/closed, z @layout, zaznaczony */
    const char *s0, *s1;      /* zakres w tekście (kopia z AST) */
    void* cache;              /* tylko UI: gotowy tekst do rysowania (C2D_Text na 3DS) */
} GNode;

typedef struct { GNode* n; int nn; /* + porty, nazwa metody, źródło */ } Graph;

Graph* gr_build(ProgramAST*, const char* src, const char* cls, const char* method, const char* layout);
void   gr_autolayout(Graph*);
int    gr_patch(Graph*, int action, ... , MsPatch* out);   /* akcja → łatka tekstu, bez zmiany grafu */
char*  gr_layout_text(Graph*);                             /* nowy blok @layout */
```

Graf trzyma wskaźniki do AST, więc żyje dokładnie tak długo jak arena parsowania i po każdej łatce budowany jest od nowa.
Rysowanie (3DS, przeglądarka) jest poza `graph.c`; rdzeń nie zależy od citro2d.

## 6. Testy (PC, w `run_tests.sh` i CI)

Na **każdym** pliku `.cs` z `tests/` i `examples/`:
1. **Zakresy:** tekst `[s0, s1)` każdej instrukcji i każdego wyrażenia parsuje się do takiego samego poddrzewa (porównanie struktury, bez numerów linii).
2. **Drukarka:** `print(parse(plik))` parsuje się do AST równego `parse(plik)`; skompilowany program daje ten sam wynik co oryginał.
3. **Tożsamość:** zbudowanie grafu i zapis bez akcji zostawia plik bajt w bajt (poza dopisanym `@layout`).
4. **Układ:** dwa razy ten sam plik → te same pozycje; `@layout` zapisany i wczytany → te same pozycje.

Oraz skrypty akcji: `minisharp --graph plik.cs Klasa.Metoda` wypisuje graf jako tekst (do testów i do debugowania),
a `--graph-edit plik.cs skrypt.txt` stosuje akcje i porównuje wynik z oczekiwanym `.cs`.

## 7. Prototyp rysowania na 3DS (równolegle z rdzeniem)

Cel: zanim powstanie edytor, wiedzieć, ile kosztuje rysowanie grafu. Log z `loch.cs` pokazał, że ~800 liter rysowanych osobno to ~10 ms na klatkę,
więc **tekst jest głównym kosztem**:
- etykieta węzła = jeden `C2D_Text` zbudowany raz przy budowaniu grafu (pole `cache`), tak jak widoczne linie w edytorze kodu,
- rysuje się tylko węzły w widoku; przy małym powiększeniu same prostokąty bez tekstu,
- przewody: krzywe złożone z 8–12 odcinków, a przy małym powiększeniu linie proste.

Pomiar: sztuczne grafy z 50 / 100 / 200 węzłów oraz prawdziwy graf `Update` z `loch.cs`, przesuwanie i powiększanie, czas rysowania w logu
(jak `editor closed: ... drawing avg`). Cel: **poniżej 8 ms przy 100 widocznych węzłach**, żeby został zapas na edytor.

**Wyniki (2026-11-06, `source3ds/graphview.c`, B w menu, SELECT = pomiar do logu; `loch.cs` na konsoli):**
- rysowanie: CPU najwyżej **7,9 ms** (najgorszy przypadek: `Game.NowyPotwor` ×4, powiększenie 0,5, 22 widoczne węzły, 617 prostokątów razem z literami),
  zwykle 2–4 ms; GPU stale ok. 2 ms. Na ekranie 320×240 i tak widać najwyżej 20–30 węzłów, więc 100 widocznych węzłów nie zdarza się w praktyce.
- **koszt to tekst**: bez tekstu (powiększenie 0,35) rysowanie trwa 0,1–0,4 ms. Litera kosztuje ok. 10 µs CPU, więc dalsze oszczędzanie = mniej liter na ekranie.
- budowanie: parsowanie całego `loch.cs` (12,7 kB) 20,5 ms, graf metody razem z przygotowaniem tekstu 1–12 ms (`Game.Draw`, 47 węzłów).
  Edycja = ok. 30 ms raz na dotknięcie, do przyjęcia; przy dużych plikach (1800 linii) warto parsować tylko zmienioną metodę.

## 8. Przeglądarka i stare grafy

Pliki z `// @graph {json}` są zwykłym C#, więc migracja to usunięcie tej linii: kod zostaje, graf zbuduje się z kodu, a pozycje dostaną automatyczny układ.
`web/grapheditor.js` przechodzi na rdzeń w wasm dopiero po edytorze na 3DS. Do tego czasu w przeglądarce nic się nie zmienia.

## 9. Kolejność pracy

1. ✔ Zakresy w AST + test 1 (zakresy) na wszystkich plikach: `minisharp --spans pliki.cs...` (test `spans` w `run_tests.sh`; 13 289 zakresów w 58 plikach). Narzędzie: `pc/astcheck.c`.
2. ✔ Drukarka (`src/printer.c`, `minisharp --print plik.cs`) + test 2: każdy plik wydrukowany na nowo parsuje się do tego samego drzewa i drukuje się tak samo; 12 programów wydrukowanych na nowo daje ten sam wynik co oryginał (`print/*` w `run_tests.sh`).
3. ✔ `gr_build` (`src/graph.c`), `--graph plik.cs [Klasa.Metoda]` (tekstowy zrzut), `--graph-layout plik.cs`, automatyczny układ, `@layout` + testy:
   graf każdej metody w każdym pliku ma dokładnie jeden węzeł na instrukcję (w `--spans`), `tests/graph_kinds.cs` (wszystkie rodzaje instrukcji,
   `@layout` z pozycją, złym rodzajem, `=open`, `=closed`), zapis i odczyt układu `loch.cs` daje te same pozycje.
   Równolegle: prototyp rysowania na 3DS na danych z `--graph` (najpierw sztuczne, potem prawdziwe).
4. ✔ Akcje → łatki (`src/graphedit.c`): zmiana wejścia (nawiasy same, gdy wejście jest składnikiem działania), wstaw po węźle / jako pierwszą
   w gałęzi (klamry same przy ciele bez `{ }`, nowe `else`, metoda `=>` zamienia się w blok), usuń (jedyne ciało `if` zostaje `{ }`),
   przesuń, rozwiń/zwiń. `gr_apply` przenosi pozycje po miejscu w tekście: węzeł, którego kod się tylko przesunął, zostaje na miejscu;
   nowy staje pod poddrzewem poprzednika i rozsuwa niższe. Test: `--graph-edit plik.cs skrypt.txt` (`tests/graph_edit.*`), wynik się kompiluje i działa.
5. Edytor grafu na 3DS (dotyk: przesuwanie, wybór węzła, paleta po nazwie, klawiatura do tekstu wejść).
6. Później: FSM jako drugi rodzaj grafu, sceny/inspektor, wasm w przeglądarce.

## 10. Duże projekty i komunikacja między klasami

W kodzie każda klasa widzi wszystko (`private` jest dziś ignorowane), więc graf nie może pokazywać połączeń „wszystko ze wszystkim”:
- **Odwołanie do innej klasy to mały węzeł wartości** (`Game.bullets`) w grafie metody, a nie przewód przez cały projekt.
  Graf metody pozostaje mały bez względu na rozmiar projektu.
- **Karty klas są domyślnie zwinięte** (nazwa + liczba pól i metod), rozwija się je dotknięciem. Do dużych klas: szukanie po nazwie
  („idź do”) i lista „gdzie używane” dla pola lub metody (z `codeindex.c`).
- **Linie zależności** (kto używa kogo) na widoku projektu są do włączenia, nie zawsze widoczne.
- **`private` jako podpowiedź:** parser zapamięta modyfikator, a paleta w *innych* klasach pokaże tylko składowe nie-`private`.
  Kompilator dalej nic nie zabrania; to tylko porządek w palecie.
- Klasa, która „zbiera wszystko” (jak `Game`), to sprawa budowy kodu, nie grafu. Graf pokaże ją uczciwie, a lekarstwem jest podział
  (np. klasa `Scene` na wspólny stan z [graphs-notes.md](graphs-notes.md)).

## Otwarte pytania

- Kierunek przepływu: z góry na dół (propozycja) czy od lewej do prawej? Do rozstrzygnięcia pomiarem i wygodą na prototypie.
- Pozycje węzłów: **ustalone** – komentarz `@layout` na końcu pliku. Pisze go tylko edytor grafu; przy pisaniu kodu nie trzeba go dotykać,
  a plik bez niego dostaje automatyczny układ.
- Zmienne sceny i zdarzenia z [graphs-notes.md](graphs-notes.md) zostają na poziomie kodu (klasa `Scene`, pola `Action`); graf pokaże je jak zwykłe pola i wywołania.
