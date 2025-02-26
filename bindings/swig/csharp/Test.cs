using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ConsoleApp3
{
    internal class Program
    {
        static void Main(string[] args)
        {
            try
            {
                Test1();
            }
            catch (AssertFailedException m)
            {

                Console.Error.WriteLine($"!!! FAIL: {m.Message} !!!");
                
                Environment.Exit(1);
            }
            Console.Error.WriteLine(":-) SUCCESS :-)");
            Environment.Exit(0);

        }

        private static void Test1()
        {
            var sf = new Sandia.SpecUtils.SpecFile();

            var pcf = @"c:\GADRAS_Dev\Detector\Generic\CZT\1.5cm-2cm-2cm\Cal.pcf";

            sf.load_file(pcf, Sandia.SpecUtils.ParserType.Pcf);

            var m = sf.measurement(0);

            Assert.AreEqual((uint)12, sf.num_measurements());

            Console.WriteLine(m.title());

            //Assert.AreEqual(m.title(), "BackgroundXX");
        }
    }
}
