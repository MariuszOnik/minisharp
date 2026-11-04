// Przykladowy projekt: kilka plikow .cs w jednym folderze kompiluje sie razem.
// D-pad / galka: statek, A: strzal, START: menu.
namespace Kosmos;

class Game {
    public static Ship ship = new Ship();
    public static Starfield stars = new Starfield(60);
    public static List<Bullet> bullets = new List<Bullet>();
    public static int shots;

    static void Update() {
        stars.Move();
        ship.Update();
        for (int i = bullets.Count - 1; i >= 0; i--) {
            bullets[i].Move();
            if (bullets[i].y < -10) bullets.RemoveAt(i);
        }
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Gfx.Rgb(8, 10, 24));
        stars.Draw();
        foreach (var b in bullets) b.Draw();
        ship.Draw();

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.DarkGray);
        Gfx.Text(10, 10, "Projekt Kosmos: Game.cs, Ship.cs, Starfield.cs", Color.White);
        Gfx.Text(10, 30, "Strzaly: " + shots + "  w locie: " + bullets.Count, Color.Yellow);
    }
}
