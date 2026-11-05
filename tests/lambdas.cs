using System;
using System.Collections.Generic;

class Counter
{
    public int count;
    public string name;
    public Action onTick;
    public Func<int, bool> limit;

    public Counter(string name) { this.name = name; }

    public Action MakeIncrement(int step)
    {
        return () => { count += step; Console.WriteLine(name + " now " + count); };
    }

    public void Tick()
    {
        count++;
        if (onTick != null) onTick();
        if (limit != null && limit(count)) Console.WriteLine(name + " hit the limit");
    }

    public int Twice(int x) { return x * 2; }
    public virtual string Who() { return "counter " + name; }
}

class Special : Counter
{
    public Special() : base("special") { }
    public override string Who() { return "special one"; }
}

class Program
{
    static int total = 0;
    static Func<int, int> twice;

    static int Square(int x) { return x * x; }
    static void Add(int x) { total += x; }
    static bool IsEven(int x) { return x % 2 == 0; }

    static Func<int> MakeCounter()
    {
        int n = 0;
        return () => { n++; return n; };
    }

    static Func<int, int> Adder(int a)
    {
        return b => a + b;
    }

    static int Apply(Func<int, int> f, int x) { return f(x); }

    static List<int> Filter(List<int> items, Predicate<int> keep)
    {
        var result = new List<int>();
        foreach (int i in items) if (keep(i)) result.Add(i);
        return result;
    }

    static void SortBy(List<string> items, Comparison<string> cmp)
    {
        for (int i = 0; i < items.Count; i++)
            for (int j = 0; j + 1 < items.Count - i; j++)
                if (cmp(items[j], items[j + 1]) > 0) { string t = items[j]; items[j] = items[j + 1]; items[j + 1] = t; }
    }

    static string Show(List<int> l)
    {
        string s = "";
        foreach (int x in l) s += x + " ";
        return s;
    }

    static void Main()
    {
        // basic lambdas
        Func<int, int> dbl = x => x * 2;
        Func<int, int, int> add = (a, b) => a + b;
        Func<float, float, float> mul = (float a, float b) => a * b;
        Action hello = () => Console.WriteLine("hello from a lambda");
        Action<string, int> rep = (s, n) => { for (int i = 0; i < n; i++) Console.Write(s); Console.WriteLine(); };
        Func<int> answer = () => 42;
        Console.WriteLine(dbl(5) + " " + add(2, 3) + " " + mul(1.5f, 4) + " " + answer());
        hello();
        rep("ab", 3);
        hello.Invoke();
        Console.WriteLine(dbl.Invoke(21));

        // closures share the variable
        int x0 = 10;
        Func<int> getX = () => x0;
        Action incX = () => x0++;
        incX(); incX();
        x0 += 100;
        Console.WriteLine("x0 " + x0 + " " + getX());

        var c1 = MakeCounter();
        var c2 = MakeCounter();
        Console.WriteLine(c1() + " " + c1() + " " + c1() + " / " + c2());

        var add5 = Adder(5);
        var add10 = Adder(10);
        Console.WriteLine(add5(1) + " " + add10(1) + " " + Apply(add5, 100) + " " + Apply(y => y - 1, 0));

        // nested lambdas
        Func<int, Func<int, int>> curry = a => b => a * 10 + b;
        Console.WriteLine(curry(3)(4));
        int outer = 1;
        Func<Func<int>> nest = () => { int mid = 2; return () => outer + mid; };
        var inner = nest();
        outer = 100;
        Console.WriteLine("nested " + inner());

        // for: one variable; foreach: a new one each time
        var fromFor = new List<Func<int>>();
        for (int i = 0; i < 3; i++) fromFor.Add(() => i);
        var fromForeach = new List<Func<int>>();
        foreach (int v in new int[] { 1, 2, 3 }) fromForeach.Add(() => v * 10);
        var fromBody = new List<Func<int>>();
        for (int i = 0; i < 3; i++) { int copy = i; fromBody.Add(() => copy); }
        string r = "";
        foreach (var f in fromFor) r += f() + " ";
        foreach (var f in fromForeach) r += f() + " ";
        foreach (var f in fromBody) r += f() + " ";
        Console.WriteLine(r);

        // method groups
        Func<int, int> sq = Square;
        Action<int> adder = Add;
        Predicate<int> even = IsEven;
        adder(3); adder(4);
        Console.WriteLine(sq(7) + " " + total + " " + even(4) + " " + even(5));
        twice = new Counter("t").Twice;
        Console.WriteLine("twice " + twice(8));
        Counter sp = new Special();
        Func<string> who = sp.Who;
        Console.WriteLine(who());

        // delegates as fields, events-like
        var cnt = new Counter("c");
        int ticks = 0;
        cnt.onTick = () => ticks++;
        cnt.limit = n => n >= 3;
        for (int i = 0; i < 4; i++) cnt.Tick();
        Console.WriteLine("ticks " + ticks);
        Action inc = cnt.MakeIncrement(5);
        inc(); inc();

        // predicates and comparisons
        var nums = new List<int> { 5, 12, 7, 20, 3, 8 };
        int limit = 6;
        Console.WriteLine(Show(Filter(nums, n => n > limit)));
        limit = 10;
        Predicate<int> big = n => n > limit;
        Console.WriteLine(Show(Filter(nums, big)) + Show(Filter(nums, IsEven)));
        var words = new List<string> { "pear", "fig", "banana", "kiwi" };
        SortBy(words, (a, b) => a.Length - b.Length);
        string ws = "";
        foreach (string w in words) ws += w + ",";
        Console.WriteLine(ws);

        // arrays of delegates, null checks
        Func<int, int>[] ops = new Func<int, int>[] { dbl, sq, y => -y };
        string o = "";
        for (int i = 0; i < ops.Length; i++) o += ops[i](3) + " ";
        Console.WriteLine(o);
        Action nothing = null;
        Console.WriteLine("null? " + (nothing == null) + " " + (hello != null));

        // recursion through a delegate
        Func<int, int> fact = null;
        fact = n => n <= 1 ? 1 : n * fact(n - 1);
        Console.WriteLine("fact " + fact(10));

        // many closures (garbage collector)
        int sum = 0;
        for (int i = 0; i < 20000; i++)
        {
            int k = i;
            Func<int> g = () => k % 7;
            sum += g();
        }
        Console.WriteLine("sum " + sum);
    }
}
