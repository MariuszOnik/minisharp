class Animal { }
class Dog : Animal { }
class Cat : Animal { }
class P { static void Main() {
    Animal a = new Cat();
    Console.WriteLine("before");
    Dog d = (Dog)a;
} }
