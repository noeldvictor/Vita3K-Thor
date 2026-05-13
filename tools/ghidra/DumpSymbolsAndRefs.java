// Ghidra headless helper for Vita3K Thor renderer investigations.
// Dumps symbols and references matching a set of text tokens.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DumpSymbolsAndRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("Usage: DumpSymbolsAndRefs <output-file> [token...]");
            return;
        }

        File output = new File(args[0]);
        File parent = output.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        List<String> tokens = new ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            tokens.add(args[i].toLowerCase());
        }

        try (PrintWriter writer = new PrintWriter(output, "UTF-8")) {
            writer.printf("Program: %s%n", currentProgram.getName());
            writer.printf("Language: %s%n", currentProgram.getLanguageID());
            writer.printf("Compiler: %s%n", currentProgram.getCompilerSpec().getCompilerSpecID());
            writer.printf("Image base: %s%n", currentProgram.getImageBase());
            writer.println();

            writer.println("== Matching Symbols ==");
            SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
            while (symbols.hasNext() && !monitor.isCancelled()) {
                Symbol symbol = symbols.next();
                String text = (symbol.getName(true) + " " + symbol.getAddress()).toLowerCase();
                if (!matches(text, tokens)) {
                    continue;
                }
                writer.printf("%s\t%s\t%s\t%s%n",
                    symbol.getAddress(),
                    symbol.getSymbolType(),
                    symbol.getSource(),
                    symbol.getName(true));

                Reference[] refs = getReferencesTo(symbol.getAddress());
                for (Reference ref : refs) {
                    writer.printf("  ref_from=%s ref_type=%s%n", ref.getFromAddress(), ref.getReferenceType());
                    Function function = getFunctionContaining(ref.getFromAddress());
                    if (function != null) {
                        writer.printf("    function=%s @ %s%n", function.getName(true), function.getEntryPoint());
                    }
                }
            }

            writer.println();
            writer.println("== Functions Matching Tokens ==");
            for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) {
                    break;
                }
                String text = (function.getName(true) + " " + function.getEntryPoint()).toLowerCase();
                if (matches(text, tokens)) {
                    writer.printf("%s\t%s%n", function.getEntryPoint(), function.getName(true));
                }
            }

            writer.println();
            writer.println("== Entrypoint Neighborhood ==");
            Address entry = currentProgram.getImageBase();
            Function entryFunction = getFunctionContaining(entry);
            if (entryFunction != null) {
                writer.printf("%s\t%s%n", entryFunction.getEntryPoint(), entryFunction.getName(true));
            }
        }
    }

    private boolean matches(String text, List<String> tokens) {
        if (tokens.isEmpty()) {
            return true;
        }
        for (String token : tokens) {
            if (text.contains(token)) {
                return true;
            }
        }
        return false;
    }
}
