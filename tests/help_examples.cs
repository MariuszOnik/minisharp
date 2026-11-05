// The examples from sdk/Help.txt (topics Klasy, Dziedziczenie, Interfejsy, Lambdy, Zdarzenia, List<T>) must compile and work.
class Gracz {
    public float x;
    public int zycia = 3;
    public static int ilu = 0;
    public Gracz(float x) { this.x = x; ilu++; }
    public void Ruch() { x += 1; }
    public bool Zyje() => zycia > 0;
    public static Gracz Nowy() { return new Gracz(0); }
}

class Wrog {
    public string imie;
    public int hp;
    public Action<Wrog> onSmierc;
    public Wrog(string imie, int hp) { this.imie = imie; this.hp = hp; }
    public virtual void Trafiony(int ile) { hp -= ile; }
    public void Lecz() { hp += 5; }
    public void Zgin() { onSmierc?.Invoke(this); }
}

class Boss : Wrog {
    public int faza = 1;
    public Boss() : base("Smok", 50) { }
    public override void Trafiony(int ile) {
        base.Trafiony(ile / 2);
        if (hp < 25) faza = 2;
    }
    public override string ToString() { return "Boss " + hp; }
}

class SuperBoss : Boss { }

abstract class Ksztalt {
    public abstract float Pole();
    public string Opis() { return "pole " + Pole(); }
}
class Kolo : Ksztalt { public float r; public override float Pole() => 3.14f * r * r; }

interface IRaniony { void Trafiony(int ile); bool Martwy(); }
interface IPodpisany { string Nazwa(); }
interface ILeczony : IRaniony { void Lecz(int ile); }
class Skrzynia : IRaniony, IPodpisany {
    int hp = 3;
    public void Trafiony(int ile) { hp -= ile; }
    public bool Martwy() => hp <= 0;
    public string Nazwa() => "skrzynia";
}
class Golem : Wrog, IPodpisany {
    public Golem() : base("Golem", 30) { }
    public string Nazwa() => imie;
}

class P {
    static int punkty = 0;
    static void PokazWybuch(Wrog w) { Console.WriteLine("bum " + w.imie); }

    static void Main() {
        var a = new Gracz(10);
        var b = new Gracz(50);
        a.Ruch();
        Gracz c = Gracz.Nowy();
        Console.WriteLine(a.x + " " + b.x + " " + Gracz.ilu + " " + c.Zyje());

        Wrog w = new Boss();
        w.Trafiony(10);
        Console.WriteLine(w.hp);
        w.Lecz();
        Console.WriteLine(w.hp + " " + w);
        if (w is Boss bb) bb.faza = 3;
        List<Wrog> wrogowie = new List<Wrog> { new Wrog("Szczur", 3), new Boss(), new SuperBoss() };
        foreach (Wrog x in wrogowie) x.Trafiony(4);
        foreach (Wrog x in wrogowie) Console.WriteLine(x.imie + " " + x.hp);
        Console.WriteLine(new Kolo().Opis());

        IRaniony r = new Skrzynia(); r.Trafiony(1);
        var cele = new List<IRaniony> { r, new Skrzynia() };
        foreach (var cc in cele) cc.Trafiony(1);
        Console.WriteLine(r.Martwy() + " " + ((IPodpisany)r).Nazwa() + " " + new Golem().Nazwa());
        Boss b2 = w as Boss;
        Boss b3 = (Boss)w;
        Console.WriteLine((b2 == b3) + " " + (w is null) + " " + (w is not Boss) + " " + (w is IRaniony));

        Wrog wrog = new Wrog("Ork", 1);
        wrog.onSmierc += x => punkty += 10;
        wrog.onSmierc += PokazWybuch;
        wrog.Zgin();
        wrog.onSmierc -= PokazWybuch;
        wrog.Zgin();
        Wrog cel = null;
        string imie = cel?.imie;
        cel?.Trafiony(5);
        string n = imie ?? "nikt";
        int hp = cel?.hp ?? 0;
        Func<int, int> pancerz = null;
        float m = pancerz?.Invoke(10) ?? 10;
        Console.WriteLine(punkty + " " + n + " " + hp + " " + m);

        var l = new List<int> { 3, 15, -2, 7, 22 };
        Console.WriteLine(l.Find(x => x > 10) + " " + l.FindIndex(x => x > 100) + " " + l.FindAll(x => x > 10).Count + " " + l.RemoveAll(x => x < 0));
        wrogowie.Sort((p, q) => q.hp - p.hp);
        Wrog pierwszy = wrogowie.Find(x => x.hp > 0 && x != w);
        Console.WriteLine(pierwszy.imie + " " + pierwszy.hp);
    }
}
