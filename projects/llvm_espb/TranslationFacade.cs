using System;
using System.IO;
using System.Text;

namespace ESPB_Runtime
{
    public static class TranslationFacade
    {
        public static string Translate(string[] args)
        {
            var stringBuilder = new StringBuilder();
            using (var stringWriter = new StringWriter(stringBuilder))
            {
                try
                {
                    Program.Run(args, stringWriter);
                }
                catch (Exception ex)
                {
                     stringWriter.WriteLine("Translation failed with an exception:");
                     stringWriter.WriteLine(ex.ToString());
                }
            }

            return stringBuilder.ToString();
        }
    }
}