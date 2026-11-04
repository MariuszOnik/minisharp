namespace Test;

// a ship
class Ship {
    public float x = 200, y;   // position
    int hp;
    public Ship() { hp = 3; }

    public void Update() {
        this./*?*/
        Fire(1);
    }

    /* draws it */
    public void Draw() { Gfx.Circle(x, y, 5, Color.Red); }

    public Bullet Fire(int power) {
        return new Bullet(x, y);
    }

    public Ship Target() { return this; }
}

class Bullet {
    public float x, y;
    public Bullet(float x0, float y0) { x = x0; y = y0; }
    public void Move() { y -= 6; }
}
