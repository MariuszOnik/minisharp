// Najprostszy program: wypisuje tekst i liczy.
namespace Hello;

class Program {
    static void Main() {
        Console.WriteLine("Hello, 3DS!");
        int sum = 0;
        for (int i = 1; i <= 10; i++) sum += i;
        Console.WriteLine("Suma 1..10 = " + sum);
    }
}
