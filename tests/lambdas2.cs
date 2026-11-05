using System.Collections;

// lambdas in coroutines, constructors, static field initializers and instance methods
class Button
{
    public string label;
    public Action onClick;
    int clicks;

    public Button(string label, int bonus)
    {
        this.label = label;
        onClick = () => { clicks += bonus; Console.WriteLine(label + " clicked, total " + clicks); };
    }
}

class Game
{
    static Func<int, int> triple = x => x * 3;
    static List<Action> later = new List<Action>();
    static int frame = 0;

    static IEnumerator Countdown(int from)
    {
        int left = from;
        Action say = () => Console.WriteLine("left " + left + " @" + frame);
        while (left > 0)
        {
            say();
            left--;
            yield return null;
        }
        later.Add(() => Console.WriteLine("countdown from " + from + " finished, left " + left));
    }

    static void Main()
    {
        var b = new Button("ok", 2);
        b.onClick();
        b.onClick();
        Console.WriteLine("triple " + triple(14));
        StartCoroutine(Countdown(3));
    }

    static void Update()
    {
        frame++;
        if (frame == 5)
        {
            foreach (Action a in later) a();
            var list = new List<int>();
            for (int i = 0; i < 5000; i++) { int k = i; Action add = () => list.Add(k); add(); }
            Console.WriteLine("list " + list.Count + " last " + list[4999]);
        }
    }
}
