# Mini Bolt: grafy przepływu na 3DS

Gałąź `mini-bolt`. Ustalenia z 2026-11-06, po teście edytora kod ↔ graf na konsoli (ta praca zostaje na `main`).

## Ustalenia

- **Graf jest źródłem, kod powstaje z grafu** (w jedną stronę, jak w przeglądarce). Z kodu nie robimy grafu.
- **Wygląd jak w Bolcie i w edytorze w przeglądarce**: przepływ od lewej do prawej, duże porty (pod palec).
- **Wspólny format z przeglądarką**: graf to JSON w ostatniej linii pliku, `// @graph {...}`; kod nad nią jest generowany.
  Graf zrobiony na telefonie otwiera się na 3DS i odwrotnie.
- **Graf zamienia się w zwykły kod**, bez interpretera grafu w czasie gry, więc na 3DS działa tak szybko jak kod pisany ręcznie.
  Plik z grafem jest w edytorze kodu tylko do czytania (ręczne zmiany graf by nadpisał).

## Generator w C (krok 1, zrobiony)

- `src/json.c`: drzewo JSON zachowujące kolejność kluczy i zapis liczb, więc zapis daje to samo co `JSON.stringify`.
- `src/fgraph.c`: katalog węzłów (zdarzenia, sterowanie, zmienne, operatory, całe API z Help, własne węzły z kodu),
  sprawdzanie grafu i generator: przeniesione 1:1 z `web/graph.js`, razem z komunikatami.
- Test: test przeglądarki zapisuje każde wywołanie generatora jako wzorzec (`MS_DUMP_GRAPHS=katalog node tests/web/graph.test.js`),
  a `minisharp --fgraph-check katalog` sprawdza, że C daje ten sam kod, te same ostrzeżenia i błędy. Wzorce są w `tests/fgraph`
  (test `fgraph` w `run_tests.sh`); CI robi je na świeżo przy każdym pushu, więc JS i C nie mogą się rozjechać.
- `minisharp --fgraph plik.json` (albo wygenerowany `.cs`) wypisuje kod z grafu.

## Własne węzły z kodu (krok 3)

```csharp
// węzeł-funkcja (bez pamięci): każda metoda static z pliku z kodem jest węzłem (działa już, także w przeglądarce)
// komentarz nad metodą to opis węzła; "// @node Tytuł" zmienia tytuł, "// @node hide" chowa metodę
static List<Point> FindPath(Grid grid, Point start, Point goal) { ... }

// węzeł z pamięcią między klatkami: klasa dziedzicząca po Node
class Counter : Node {
    [In]  public int step = 1;
    [Out] public int total;
    [Out] public bool overTen;
    public override void Run() { total += step; overTen = total > 10; }
}
```
Z grafu powstaje `static Counter n3 = new Counter();`, a w przepływie `n3.step = ...; n3.Run();`, potem wyjścia to `n3.total`.
Do zrobienia: atrybuty `[In]` / `[Out]` w parserze (dalej poprawny C#), klasa bazowa `Node` w SDK.

## Podgrafy (krok 3b)

Węzeł złożony z innych węzłów (Super Unit / Macro w Bolcie): graf z węzłem „Wejścia” (porty, które deklarujesz)
i węzłem „Wyjścia”. W palecie jest zwykłym węzłem, dotknięcie go „wchodzi do środka”. Powstaje z niego metoda `static`
z parametrami i wynikiem albo klasa `Node`, jeśli ma pamiętać stan między klatkami.

## Odpowiedniki z Unity / Bolta

| Bolt | Mini Bolt |
|---|---|
| Flow Machine na GameObject | graf w trybie „z obiektami”: każdy obiekt ma własne zmienne |
| Macro, Super Unit | podgraf (krok 3b) |
| zmienne grafu / obiektu / sceny | pola grafu / pola obiektu / klasa `Scene` |
| Start, Update | Main, Update, Draw (hooki: każdy graf ma własne) |
| State Machine | później |

## Kolejność

1. ✔ Format i generator w C, zgodne z przeglądarką.
2. Edytor na 3DS: przepływ od lewej do prawej, wygląd jak w przeglądarce, duże porty, łączenie przeciągnięciem
   albo dotknięciem dwóch portów, paleta z wyszukiwaniem (`fg_search`).
   **Usuwanie połączeń (pomyłki):** dotknięcie przewodu zaznacza go (szeroka strefa trafienia, jak w przeglądarce),
   „Usuń” kasuje zaznaczony przewód albo węzeł razem z jego przewodami; przeciągnięcie przewodu z podłączonego wejścia
   w puste miejsce odpina go; „Cofnij” wraca o krok. Rysowanie i dotyk z `source3ds/gview.c`
   (pomiary z `docs/graph-model.md`, sekcja 7: kosztem jest tekst).
3. Własne węzły: metody static (są) i klasy `Node` z `[In]` / `[Out]`; 3b: podgrafy.
4. Grafy obiektów i scena: obiekty, inspektor, zmienne sceny.
