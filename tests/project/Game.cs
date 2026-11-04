using System;

class Game {
    static void Main() {
        var p = new Player("Ala", 3);
        p.Hit(1);
        Console.WriteLine(p.Describe());
        Console.WriteLine(Util.Twice(21));
        Console.WriteLine(Util.Greeting);
    }
}
