using System.Collections;

// virtual coroutines and ToString called from inside a coroutine
abstract class Unit
{
    public string name;
    public Unit(string name) { this.name = name; }
    public abstract IEnumerator Act();
    public virtual IEnumerator Wait(int frames)
    {
        for (int i = 0; i < frames; i++) yield return null;
    }
    public override string ToString() { return "Unit " + name; }
}

class Archer : Unit
{
    public Archer() : base("archer") { }
    public override IEnumerator Act()
    {
        Console.WriteLine(this + " aims");
        yield return Wait(2);
        Console.WriteLine(this + " shoots");
    }
}

class Knight : Unit
{
    public Knight() : base("knight") { }
    public override IEnumerator Act()
    {
        Console.WriteLine(ToString() + " charges");
        yield return Wait(1);
        Console.WriteLine(base.ToString() + " hits");
    }
    public override IEnumerator Wait(int frames)
    {
        Console.WriteLine("knight waits " + frames);
        yield return base.Wait(frames + 1);
    }
}

class Game
{
    static int frame = 0;
    static void Main()
    {
        Unit[] units = new Unit[] { new Archer(), new Knight() };
        foreach (Unit u in units) StartCoroutine(u.Act());
    }
    static void Update()
    {
        frame++;
        if (frame <= 4) Console.WriteLine("frame " + frame);
    }
}
