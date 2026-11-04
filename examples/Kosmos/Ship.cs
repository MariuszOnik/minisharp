namespace Kosmos;

class Ship {
    public float x = 200, y = 200;

    public void Update() {
        if (Input.Held(Btn.Left)) x -= 3;
        if (Input.Held(Btn.Right)) x += 3;
        if (Input.Held(Btn.Up)) y -= 3;
        if (Input.Held(Btn.Down)) y += 3;
        x += Input.StickX() * 4;
        y += Input.StickY() * 4;
        x = Mathf.Clamp(x, 12, 388);
        y = Mathf.Clamp(y, 12, 228);
        if (Input.Pressed(Btn.A)) {
            Game.bullets.Add(new Bullet(x, y - 12));
            Game.shots++;
        }
    }

    public void Draw() {
        Gfx.Line(x, y - 12, x - 10, y + 10, Color.Cyan);
        Gfx.Line(x, y - 12, x + 10, y + 10, Color.Cyan);
        Gfx.Line(x - 10, y + 10, x + 10, y + 10, Color.Cyan);
        Gfx.Circle(x, y + 12, 3, Color.Orange);
    }
}

class Bullet {
    public float x, y;
    public Bullet(float x0, float y0) { x = x0; y = y0; }
    public void Move() { y -= 6; }
    public void Draw() { Gfx.Rect(x - 1, y - 4, 2, 8, Color.Yellow); }
}
