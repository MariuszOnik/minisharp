using System;
using System.Collections.Generic;

class Enemy
{
    public string name;
    public int hp;
    public Enemy target;
    public Action<Enemy> onDeath;
    public Func<int, int> armor;

    public Enemy(string name, int hp) { this.name = name; this.hp = hp; }

    public void Hit(int dmg)
    {
        int d = armor?.Invoke(dmg) ?? dmg;       // armor may be null
        hp -= d;
        Console.WriteLine(name + " takes " + d + ", hp " + hp);
        if (hp <= 0) onDeath?.Invoke(this);
    }

    public override string ToString() { return name + "(" + hp + ")"; }
}

class Program
{
    static int deaths = 0;
    static void CountDeath(Enemy e) { deaths++; }
    static void Announce(Enemy e) { Console.WriteLine("announce: " + e.name + " died"); }

    static string Ints(List<int> l)
    {
        string s = "[";
        for (int i = 0; i < l.Count; i++) s += (i > 0 ? " " : "") + l[i];
        return s + "]";
    }

    static string Names(List<Enemy> l)
    {
        string s = "[";
        for (int i = 0; i < l.Count; i++) s += (i > 0 ? " " : "") + l[i];
        return s + "]";
    }

    static void Main()
    {
        // ?. and ??
        Enemy a = new Enemy("orc", 10);
        Enemy none = null;
        Console.WriteLine("name: " + (none?.name ?? "nobody") + " / " + (a?.name ?? "nobody"));
        Console.WriteLine("target: " + (a.target?.name ?? "none"));
        none?.Hit(5);                       // nothing happens
        string s = none?.ToString();
        Console.WriteLine("s is null: " + (s == null));
        a.target = new Enemy("elf", 4);
        Console.WriteLine("target: " + a.target?.name + " len " + a.target?.name.Length);
        string pick = null;
        pick = pick ?? "first";
        pick = pick ?? "second";
        Console.WriteLine(pick);

        // events: += and -=
        a.onDeath += CountDeath;
        a.onDeath += Announce;
        a.onDeath += e => Console.WriteLine("lambda sees " + e.name);
        a.armor = d => d / 2;
        a.Hit(8);
        a.Hit(8);
        a.Hit(8);
        Console.WriteLine("deaths " + deaths);
        a.onDeath -= Announce;
        a.hp = 1; a.Hit(1);
        Console.WriteLine("deaths " + deaths);
        a.onDeath -= CountDeath;
        a.onDeath -= Announce;              // not there any more: nothing changes
        a.hp = 1; a.armor = null; a.Hit(3);
        Console.WriteLine("deaths " + deaths);
        Action chain = null;
        int calls = 0;
        Action one = () => calls++;
        chain += one; chain += one; chain += () => calls += 10;
        chain();
        chain -= one;
        chain();
        Console.WriteLine("calls " + calls);
        Func<int> f = () => 1;
        f += () => 2;
        Console.WriteLine("last result " + f());

        // List methods with delegates
        var nums = new List<int> { 5, 12, 7, 20, 3, 8, 15 };
        Console.WriteLine(nums.Find(n => n > 6) + " " + nums.FindLast(n => n > 6) + " " + nums.Find(n => n > 100));
        Console.WriteLine(nums.FindIndex(n => n % 2 == 0) + " " + nums.FindLastIndex(n => n % 2 == 0) + " " + nums.FindIndex(n => n < 0));
        Console.WriteLine(nums.Exists(n => n == 7) + " " + nums.Exists(n => n == 8000) + " " + nums.TrueForAll(n => n > 0) + " " + nums.TrueForAll(n => n > 5));
        Console.WriteLine(Ints(nums.FindAll(n => n >= 8)));
        int sum = 0;
        nums.ForEach(n => sum += n);
        Console.WriteLine("sum " + sum);
        var copy = new List<int>(nums);
        Console.WriteLine("removed " + copy.RemoveAll(n => n % 2 == 1) + " " + Ints(copy));
        nums.Sort();
        Console.WriteLine(Ints(nums));
        nums.Sort((x, y) => y - x);
        Console.WriteLine(Ints(nums));
        nums.Reverse();
        Console.WriteLine(Ints(nums));

        var fl = new List<float> { 2.5f, -1, 0.25f, 10 };
        fl.Sort();
        string fs = "";
        foreach (float x in fl) fs += x + " ";
        Console.WriteLine(fs);

        var words = new List<string> { "pear", "Apple", "fig", "banana", "apple", "Cherry" };
        words.Sort();
        string ws = "";
        foreach (string w in words) ws += w + " ";
        Console.WriteLine(ws);
        words = new List<string> { "melon", "kiwi", "watermelon", "fig", "banana" };
        words.Sort((x, y) => x.Length - y.Length);
        ws = "";
        foreach (string w in words) ws += w + " ";
        Console.WriteLine(ws);

        var enemies = new List<Enemy> { new Enemy("bat", 3), new Enemy("troll", 40), new Enemy("rat", 1), new Enemy("ogre", 25) };
        enemies.Sort((x, y) => x.hp - y.hp);
        Console.WriteLine(Names(enemies));
        Enemy strong = enemies.Find(e => e.hp > 20);
        Console.WriteLine("first strong: " + strong + ", none: " + (enemies.Find(e => e.hp > 1000) == null));
        enemies.ForEach(e => e.hp *= 2);
        int limit = 10;
        enemies.RemoveAll(e => e.hp < limit);
        Console.WriteLine(Names(enemies));
        Console.WriteLine(enemies.FindAll(e => e.name.Contains("o")).Count);
    }
}
