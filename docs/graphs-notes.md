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
