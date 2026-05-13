// Ghidra headless helper for Vita3K Thor investigations.
// Dumps the function and nearby instructions for a specific address.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.math.BigInteger;

public class DumpAddressContext extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            printerr("Usage: DumpAddressContext <output-file> <address> [before-bytes] [after-bytes]");
            return;
        }

        File output = new File(args[0]);
        File parent = output.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[1]);
        long before = args.length >= 3 ? Long.decode(args[2]) : 0x80;
        long after = args.length >= 4 ? Long.decode(args[3]) : 0x100;
        Address start = address.subtractNoWrap(Math.min(before, address.getOffset()));
        Address end = address.addNoWrap(after);

        try (PrintWriter writer = new PrintWriter(output, "UTF-8")) {
            writer.printf("Program: %s%n", currentProgram.getName());
            writer.printf("Language: %s%n", currentProgram.getLanguageID());
            writer.printf("Compiler: %s%n", currentProgram.getCompilerSpec().getCompilerSpecID());
            writer.printf("Image base: %s%n", currentProgram.getImageBase());
            writer.printf("Address: %s%n", address);
            writer.println();

            for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
                if (block.contains(address) || block.contains(start)) {
                    block.setRead(true);
                    block.setExecute(true);
                }
            }

            writer.println("== Memory Blocks ==");
            for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
                writer.printf("%s - %s  %s  r=%s w=%s x=%s%n",
                    block.getStart(),
                    block.getEnd(),
                    block.getName(),
                    block.isRead(),
                    block.isWrite(),
                    block.isExecute());
            }
            writer.println();

            Function function = getFunctionContaining(address);
            writer.println("== Function ==");
            if (function != null) {
                writer.printf("%s @ %s%n", function.getName(true), function.getEntryPoint());
            } else {
                writer.println("(no function)");
            }
            writer.println();

            writer.println("== References To Address ==");
            Reference[] refs = getReferencesTo(address);
            if (refs.length == 0) {
                writer.println("(none)");
            }
            for (Reference ref : refs) {
                writer.printf("%s %s%n", ref.getFromAddress(), ref.getReferenceType());
            }
            writer.println();

            writer.println("== Instructions ==");
            Listing listing = currentProgram.getListing();
            if (listing.getInstructionAt(address) == null) {
                Register thumbMode = currentProgram.getProgramContext().getRegister("TMode");
                if (thumbMode != null) {
                    currentProgram.getProgramContext().setValue(thumbMode, start, end, BigInteger.ONE);
                }
                disassemble(start);
                disassemble(address);
            }

            InstructionIterator instructions = listing.getInstructions(start, true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                Address instrAddress = instruction.getAddress();
                if (instrAddress.compareTo(end) > 0) {
                    break;
                }

                String marker = instrAddress.equals(address) ? "=>" : "  ";
                writer.printf("%s %s  %s%n",
                    marker,
                    instrAddress,
                    instruction.toString());
            }
        }
    }
}
