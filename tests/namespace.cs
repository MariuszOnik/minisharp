using System;
namespace My.Game {
    class A { public static int V() { return 7; } }
    class Program {
        static void Main() { Console.WriteLine("ns " + A.V()); }
    }
}
