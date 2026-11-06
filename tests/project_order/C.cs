[Order(-5)]
class C {
    static void Main() { Console.WriteLine("Main C"); }
    static void Update() { Console.WriteLine("Update C"); }
}

// equal numbers keep the order of the classes: D after B
class D {
    static void Update() { Console.WriteLine("Update D"); }
}
