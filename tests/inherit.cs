using System;
using System.Collections.Generic;

interface IDamageable
{
    void TakeDamage(int amount);
    bool IsDead();
}

interface IHealable : IDamageable
{
    void Heal(int amount);
}

interface INamed
{
    string GetName();
}

abstract class Entity : INamed
{
    public static int created = 0;
    public string name;
    public float x, y;
    protected int id;

    public Entity(string name, float x, float y)
    {
        this.name = name;
        this.x = x;
        this.y = y;
        created++;
        id = created;
        Console.WriteLine("Entity ctor: " + name + " #" + id);
    }

    public string GetName() { return name; }

    public abstract string Kind();

    public virtual void Update(float dt)
    {
        x += dt;
    }

    public virtual string Describe()
    {
        return Kind() + " " + name + " at " + x + "," + y;
    }

    public override string ToString()
    {
        return "<" + Kind() + ":" + name + ">";
    }

    public static string Count() { return "created " + created; }
}

class Enemy : Entity, IDamageable
{
    public int hp = 10;
    public int speed;

    public Enemy(string name, int speed) : base(name, 0, 5)
    {
        this.speed = speed;
        Console.WriteLine("Enemy ctor: hp=" + hp + " speed=" + speed);
    }

    public override string Kind() { return "Enemy"; }

    public override void Update(float dt)
    {
        base.Update(dt * speed);
        y -= 1;
    }

    public virtual void TakeDamage(int amount)
    {
        hp -= amount;
        if (hp < 0) hp = 0;
    }

    public bool IsDead() { return hp <= 0; }
}

class Boss : Enemy, IHealable
{
    public int phase = 1;

    public Boss(string name) : base(name.ToUpper(), 2)
    {
        hp = 50;
        Console.WriteLine("Boss ctor: hp=" + hp);
    }

    public override string Kind() { return "Boss"; }

    public override void TakeDamage(int amount)
    {
        base.TakeDamage(amount / 2);
        if (hp < 25) phase = 2;
    }

    public void Heal(int amount) { hp += amount; }

    public override string Describe()
    {
        return base.Describe() + " phase " + phase;
    }

    public override string ToString()
    {
        return "Boss(" + base.ToString() + ")";
    }
}

class Pickup : Entity
{
    public Pickup(string name) : base(name, 1, 1) { }
    public override string Kind() { return "Pickup"; }
}

class Plain
{
    public int v = 3;
}

class Plain2 : Plain
{
    public int w = 4;
    public int Sum() { return v + w; }
}

class Shape
{
    public virtual float Area() { return 0; }
    public string Info() { return GetType2() + " area " + Area(); }
    public virtual string GetType2() { return "Shape"; }
}

class Circle : Shape
{
    public float r;
    public Circle(float r) { this.r = r; }
    public override float Area() { return 3 * r * r; }
    public override string GetType2() { return "Circle"; }
}

class Rect : Shape
{
    public float w, h;
    public Rect(float w, float h) { this.w = w; this.h = h; }
    public override float Area() { return w * h; }
    public override string GetType2() { return "Rect"; }
}

class Square : Rect
{
    public Square(float s) : base(s, s) { }
    public override string GetType2() { return "Square"; }
}

class Program
{
    static void Hit(IDamageable d, int n)
    {
        d.TakeDamage(n);
        Console.WriteLine("hit " + n + " dead=" + d.IsDead());
    }

    static void Main()
    {
        var e = new Enemy("goblin", 3);
        var b = new Boss("dragon");
        var p = new Pickup("coin");
        Console.WriteLine(Entity.Count());

        List<Entity> all = new List<Entity> { e, b, p };
        foreach (Entity en in all)
        {
            en.Update(0.5f);
            Console.WriteLine(en.Describe());
            Console.WriteLine(en);
            Console.WriteLine("str: " + en + " name=" + en.GetName());
        }

        Hit(e, 4);
        Hit(e, 7);
        Hit(b, 30);
        Console.WriteLine("boss hp=" + b.hp + " phase=" + b.phase);
        IHealable h = b;
        h.Heal(5);
        h.TakeDamage(2);
        Console.WriteLine("boss hp=" + b.hp + " dead=" + h.IsDead());

        INamed nm = p;
        Console.WriteLine("named: " + nm.GetName());

        // is / as / casts
        foreach (Entity en in all)
        {
            if (en is Boss bb) Console.WriteLine("boss phase " + bb.phase);
            else if (en is Enemy) Console.WriteLine("an enemy with hp " + ((Enemy)en).hp);
            if (en is IDamageable) Console.WriteLine(en.name + " can be damaged");
            if (en is not IHealable) Console.WriteLine(en.name + " cannot heal");
            Enemy asEnemy = en as Enemy;
            Console.WriteLine("as Enemy null? " + (asEnemy == null));
            IHealable hh = en as IHealable;
            if (hh != null) hh.Heal(1);
        }
        Entity nothing = null;
        Console.WriteLine("null is Enemy: " + (nothing is Enemy));
        Console.WriteLine("null is null: " + (nothing is null) + " e is not null: " + (e is not null));
        Enemy fromNull = (Enemy)nothing;
        Console.WriteLine("cast of null: " + (fromNull == null));
        Entity up = b;
        Console.WriteLine("same object: " + (up == b) + " " + (up != e));

        Plain2 p2 = new Plain2();
        Plain pl = p2;
        Console.WriteLine("plain " + pl.v + " " + p2.Sum() + " " + pl);

        Shape[] shapes = new Shape[] { new Circle(2), new Rect(2, 3), new Square(4), new Shape() };
        float total = 0;
        foreach (Shape s in shapes)
        {
            Console.WriteLine(s.Info());
            total += s.Area();
        }
        Console.WriteLine("total " + total);
        Shape pick = shapes.Length > 2 ? shapes[2] : null;
        Rect rr = pick as Rect;
        Console.WriteLine("square as rect: " + rr.w + "x" + rr.h + " " + rr.ToString());
        Console.WriteLine(b.ToString() + " " + e.ToString());
        Console.WriteLine(Entity.created + " " + Enemy.Count());
    }
}
