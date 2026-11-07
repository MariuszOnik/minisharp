// Attributes: [Order(n)] on a class, [In] / [Out] on fields (ports of a graph node, used later).
[Order(-1)]
class Licznik {
    [In] public int krok = 1;
    [Out] public int suma;
    [In, Out] public float x;
    [In]
    [Out] public bool flaga;
    public void Run() { suma += krok; }

    static void Main() {
        var l = new Licznik();
        l.Run(); l.Run();
        Console.WriteLine(l.suma);
    }
}
