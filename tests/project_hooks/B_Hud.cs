class Hud {
    static int ticks;
    static void Main() { Console.WriteLine("Hud.Main"); }
    static void Update() { ticks += 10; Console.WriteLine("Hud.Update " + ticks); }
    static void Draw() { Console.WriteLine("Hud.Draw " + ticks); }
}
