// Liste les fonctions dans une plage RVA, avec appelants. Args: <outfile> <rvaStart> <rvaEnd>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
public class ListFuncs extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        long s = Long.parseLong(a[1].replace("0x",""),16), e = Long.parseLong(a[2].replace("0x",""),16);
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(toAddr(base+s), true);
        while (it.hasNext()) {
            Function f = it.next();
            long r = f.getEntryPoint().getOffset()-base;
            if (r > e) break;
            StringBuilder sb = new StringBuilder();
            for (Reference ref : getReferencesTo(f.getEntryPoint())) {
                Function c = getFunctionContaining(ref.getFromAddress());
                sb.append(" ").append(c==null? "?"+Long.toHexString(ref.getFromAddress().getOffset()-base) : Long.toHexString(c.getEntryPoint().getOffset()-base));
            }
            out.println("0x"+Long.toHexString(r)+" size "+f.getBody().getNumAddresses()+" <-"+sb);
        }
        out.close();
    }
}
