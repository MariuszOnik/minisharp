// sorting and filtering big lists with lambdas that make garbage (the collector runs in the middle)
class Item
{
    public string name;
    public int v;
    public Item(int v) { this.v = v; name = "item" + v; }
}

class P
{
    static void Main()
    {
        var items = new List<Item>();
        Random.Seed(42);
        for (int i = 0; i < 3000; i++) items.Add(new Item(Random.Range(0, 1000000)));
        int calls = 0;
        items.Sort((a, b) => { calls++; string x = a.name + "!"; string y = b.name + "?"; return a.v - b.v + (x.Length - y.Length) * 0; });
        bool ok = true;
        for (int i = 1; i < items.Count; i++) if (items[i - 1].v > items[i].v) ok = false;
        Console.WriteLine("sorted " + ok + " calls>0 " + (calls > 0));
        var big = items.FindAll(it => (it.name + it.v).Length > 0 && it.v > 500000);
        bool ok2 = big.TrueForAll(it => it.v > 500000) && big.Count + items.FindAll(it => it.v <= 500000).Count == 3000;
        Console.WriteLine("filtered " + ok2);
        Action all = null;
        int n = 0;
        for (int i = 0; i < 200; i++) { int k = i; all += () => n += k; }
        all();
        Console.WriteLine("events " + n);
    }
}
