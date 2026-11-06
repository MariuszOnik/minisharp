class Game {
    static int frame;
    static void Main() { Console.WriteLine("Game.Main"); }
    static void Update() { frame++; Console.WriteLine("Game.Update " + frame); }
    static void Draw() { Console.WriteLine("Game.Draw " + frame); }
}
