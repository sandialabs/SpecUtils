using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ConsoleApp3
{
    internal class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("Hello!");

            var sf = new Sandia.SpecUtils.SpecFile();

            sf.load_file(@"..\..\..\..\unit_tests\test_data\spectra\Example1.pcf", Sandia.SpecUtils.ParserType.Pcf);

            var m = sf.measurement(1);

            Console.WriteLine(m.title());
        }
    }
}
