interface IHit { void Hit(int dmg); }
interface IHeal : IHit { void Heal(int n); }

abstract class Entity
{
    public float x, y;
    public string name;
    public abstract void Update();
    public virtual string Info() { return name; }
}

class Enemy : Entity, IHit
{
    public int hp;
    public override void Update() { base./*?*/ }
    public void Hit(int dmg) { hp -= dmg; }
}

class Boss : Enemy, IHeal
{
    public int phase;
    public void Heal(int n) { hp += n; }
    public override string Info() { return "boss"; }
    void Test()
    {
        name./*?*/
        this./*?*/
    }
}

class Game
{
    static Boss boss;
    static IHeal healer;
    static void Main()
    {
        boss./*?*/
        boss.name./*?*/
        healer./*?*/
        var e = new Enemy();
        e./*?*/
    }
}
