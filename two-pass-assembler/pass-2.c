#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "utils.h"

int passTwo(FILE *, FILE *, FILE *);

// hardest. Need to use some creativity.
unsigned long long int assemble_instruction(char *, char *, int);
void get_literal_value(char *, char *);
unsigned long long int get_string_literal_hex(char *);
int get_object_code_length(unsigned long long int);
void update_text_record_length(FILE *, int, int);
void update_text_record_start_address(FILE *, int, int);

int increment_pc();
void init_pc_file();

FILE *PROGRAM_COUNTER_FILE;
int PROGRAM_COUNTER;
int BASE = 0;

int main()
{
    // input files
    FILE *input_file = fopen("pass-1-outputs/intermediate.txt", "r");

    // output files
    CreateDirectory("pass-2-outputs", NULL);
    FILE *object_program = fopen("pass-2-outputs/object_program.txt", "w");
    FILE *assembly_listing = fopen("pass-2-outputs/program_listing.txt", "w");
    // Input file is an assembly program.
    // The program is written in a fixed format with fields
    // LABEL, OPCODE and OPERAND

    if (passTwo(input_file, object_program, assembly_listing) == ERROR_VALUE)
        printf("Assembly failed.\n");
    else
        printf("Pass 2 of 2 of two completed successfully.\n");

    fclose(input_file);
    fclose(object_program);
    fclose(assembly_listing);

    return 0;
}

int passTwo(FILE *input_file, FILE *object_program, FILE *assembly_listing)
{
    int resb_resw_previously = 0; // set to 1, if the previous OPCODE was RESB or RESB
    int num_instructions_in_one_text_record = 0;
    char program_name[MAX_TOKEN_LENGTH];
    int start_address;
    int length;

    int location;
    char label[MAX_TOKEN_LENGTH];
    char mnemonic[MAX_TOKEN_LENGTH];
    char operand[MAX_TOKEN_LENGTH];

    fscanf(input_file, "%s\t%s\t%x", program_name, mnemonic, &start_address);

    if (strcmp(mnemonic, "START") == 0)
        fprintf(assembly_listing, "%4s%10s%10x\n", program_name, mnemonic, start_address);
    else
    {
        printf("ERROR: Assembler expects START opcode in line 1.\n");
        return ERROR_VALUE;
    }

    // Program length in decimal.
    FILE *program_length = fopen("pass-1-outputs/program_length.txt", "r");
    fscanf(program_length, "%d", &length);
    fclose(program_length);

    // Write header record to object program.
    fprintf(object_program, "H%-6s%06x%06x\n", program_name, start_address, length);

    int text_record_length = 0;
    int text_record_start_address = start_address;
    FILE *temp_text_record = fopen("pass-2-outputs/Temp_text_record.txt", "w");
    fprintf(temp_text_record, "%c %06x %02x", 'T', text_record_start_address, text_record_length);
    // Later we have to use fseek() and fputc() to replace 0 (text record length)
    // to appropriate value.

    unsigned long long int assembled_object_code = 0;
    init_pc_file();

    while (fscanf(input_file, "%x\t%s\t%s\t%s", &location, label, mnemonic, operand) > 0)
    {
        increment_pc();
        // No comments in intermediate file.
        if (opcode_search(mnemonic))
        {
            int symbol_address = 0;
            if (strcmp(operand, EMPTY) != 0)
            {
                // If instruction is format 2, then operand will be registers.
                if (opcode_instruction_format(mnemonic) != 2 && symbol_search(operand))
                    symbol_address = symbol_value(operand);
                else if (opcode_instruction_format(mnemonic) != 2)
                {
                    printf("ERROR: %s doesn't exist in SYMTAB.\n", operand);
                    return ERROR_VALUE;
                }
            }

            // assemble object code
            assembled_object_code = assemble_instruction(mnemonic, operand, symbol_address);
        }
        else if (strcmp(mnemonic, "BYTE") == 0)
        {
            char operand_without_extraneous[MAX_TOKEN_LENGTH] = "";
            get_literal_value(operand_without_extraneous, operand);

            if (operand[0] == 'X')
                assembled_object_code = strtol(operand_without_extraneous, NULL, 16);
            else if (operand[0] == 'C')
                assembled_object_code = get_string_literal_hex(operand_without_extraneous);
            else
            {
                printf("ERROR: Unknown literal %s\n", operand);
                return ERROR_VALUE;
            }
        }
        else if (strcmp(mnemonic, "WORD") == 0)
            assembled_object_code = (unsigned long long int)strtol(operand, NULL, 16);
        else if (strcmp(mnemonic, "BASE") == 0)
        {
            BASE = symbol_value(operand);
            continue;
        }
        else if (strcmp(mnemonic, "RESW") == 0 || strcmp(mnemonic, "RESB") == 0)
        {
            fprintf(assembly_listing, "%04x%10s%10s%10s\n", location, label, mnemonic, operand);

            // Break into a new text record,
            // but don't create another if the previous instruction
            // was also an assembler directive.

            if (!resb_resw_previously)
            {
                int obj_code_length = get_object_code_length(assembled_object_code);
                fprintf(temp_text_record, "\n");
                update_text_record_length(temp_text_record, text_record_length, num_instructions_in_one_text_record);
                num_instructions_in_one_text_record = 0;

                // Start new text_record.
                text_record_length = 0;
                text_record_start_address = location;

                resb_resw_previously = 1;
            }
            continue;
        }

        if (resb_resw_previously)
            update_text_record_start_address(temp_text_record, location, text_record_length);

        int obj_code_length = get_object_code_length(assembled_object_code);

        if (text_record_length + obj_code_length > 30) // 30 bytes take up 60 columns, which is maximum that one text record can hold.
        {
            fprintf(temp_text_record, "\n");
            update_text_record_length(temp_text_record, text_record_length, num_instructions_in_one_text_record);
            num_instructions_in_one_text_record = 0;

            // Start new text_record
            text_record_length = obj_code_length;
            text_record_start_address = location;
            fprintf(temp_text_record, "%c %06x %02x", 'T', text_record_start_address, text_record_length);
        }
        else if (strcmp(mnemonic, "EQU") != 0)
            text_record_length += obj_code_length;

        // Write the assembled object code.
        // %0*x is variable padding length, length is obj_code_length.
        // Multiply by 2 to obj_code_length for leading zeroes.
        if (strcmp(mnemonic, "EQU") != 0)
        {
            // No object code for for EQU.
            fprintf(temp_text_record, " %0*llx", 2 * obj_code_length, assembled_object_code);
            num_instructions_in_one_text_record++;
        }

        if (strcmp(mnemonic, "EQU") == 0)
            fprintf(assembly_listing, "%04x%10s%10s%10s%4s\n", location, label, mnemonic, operand, " ");
        else if (strcmp(mnemonic, "END") != 0)
            fprintf(assembly_listing, "%04x%10s%10s%10s%4s%0*llx\n", location, label, mnemonic, operand, " ", 2 * obj_code_length, assembled_object_code);
        else
            fprintf(assembly_listing, "%4s%10s%10s%10s\n", EMPTY, label, mnemonic, EMPTY);

        resb_resw_previously = 0;
    }

    fprintf(temp_text_record, "\n");
    update_text_record_length(temp_text_record, text_record_length, num_instructions_in_one_text_record);

    fclose(temp_text_record);

    // Write to object program
    temp_text_record = fopen("pass-2-outputs/Temp_text_record.txt", "r");

    for (char ch = fgetc(temp_text_record); ch != EOF; ch = fgetc(temp_text_record))
        fputc(ch, object_program);

    fclose(temp_text_record);
    remove("pass-2-outputs/Temp_text_record.txt");

    // Write the end record
    // This is not really the correct logic
    // Refer README.md
    fprintf(object_program, "E%06x\n", start_address);
    fclose(PROGRAM_COUNTER_FILE);

    return 1;
}

unsigned long long int assemble_instruction(char mnemonic[], char operand[], int symbol_address)
{
    // Returns an assembled object code from given input parameters.
    unsigned long long int assembled_object_code;
    int instruction_format = opcode_instruction_format(mnemonic);

    // RSUB is a sole special case
    // We will handle it first
    if (strcmp(mnemonic, "RSUB") == 0)
    {
        // opcode ni = 0b 0000 0011
        assembled_object_code = opcode_value(mnemonic) + 0b00000011;
        assembled_object_code <<= 16;

        return assembled_object_code;
    }

    if (instruction_format == 1)
        assembled_object_code = opcode_value(mnemonic);
    else if (instruction_format == 2)
    {
        // Register table:
        // PC and SW are not needed, however SW can be used for error flags.
        // For now its not needed.
        char registers[NUM_REGISTERS] = {'A', 'X', 'L', 'B', 'S', 'T', 'F'};
        int r1 = 0, r2 = 0;

        assembled_object_code = opcode_value(mnemonic);

        // Find r1 number
        for (int reg_number = 0; reg_number < NUM_REGISTERS; reg_number++)
            if (operand[0] == registers[reg_number])
                r1 = reg_number;

        // Find r2 number
        if (strlen(operand) == 3) // If there are two registers in operand, then only.
            for (int reg_number = 0; reg_number < NUM_REGISTERS; reg_number++)
                if (operand[2] == registers[reg_number])
                    r2 = reg_number;

        assembled_object_code <<= 4;
        assembled_object_code += r1;

        assembled_object_code <<= 4;
        assembled_object_code += r2;
    }
    else if (instruction_format == 3 || instruction_format == 4)
    {
        assembled_object_code = opcode_value(mnemonic);
        // Check if indirect
        if (operand[0] == '@')
            // Set n flag on;
            assembled_object_code += 2;
        // Check if immediate
        else if (operand[0] == '#')
            // Set i flag on;
            assembled_object_code += 1;
        else
            // neither immediate nor indirect
            assembled_object_code += 3;
        // Dealing with x, b, p, e flags
        // We don't need any other registers as variables other than program counter and base

        // X flag
        assembled_object_code <<= 1;
        if (operand[strlen(operand) - 1] == 'X')
            assembled_object_code += 1; // Set X flag to 1;

        // B and P flags
        assembled_object_code <<= 2;
        int displacement;

        if (is_immediate_number(operand) || instruction_format == 4)
            // value too big to be addressed from base relative or PC relative.
            // It will be a format 4 instruction.
            displacement = symbol_address;
        else if (-2048 <= (symbol_address - PROGRAM_COUNTER) &&
                 (symbol_address - PROGRAM_COUNTER) <= +2047)
        {
            // Set P flag to 1;
            displacement = symbol_address - PROGRAM_COUNTER;
            assembled_object_code += 1;
        }
        else if (0 <= (symbol_address - BASE) &&
                 (symbol_address - BASE) <= 4095)
        {
            displacement = symbol_address - BASE;
            assembled_object_code += 2;
        } // Set B flag to 1;

        // Set E flag
        assembled_object_code <<= 1;
        if (instruction_format == 4)
            assembled_object_code += 1;

        // Add displacement field.
        if (instruction_format == 3)
        {
            displacement &= 0x00000fff;
            assembled_object_code <<= 12;
        }
        else if (instruction_format == 4)
        {
            displacement &= 0x000fffff;
            assembled_object_code <<= 20;
        }

        assembled_object_code += displacement;
    }

    return assembled_object_code;
}

void init_pc_file()
{
    PROGRAM_COUNTER_FILE = fopen("pass-1-outputs/intermediate.txt", "r");

    // Read first line and ignore.
    fscanf(PROGRAM_COUNTER_FILE, "%*s\t%*s\t%*s");

    // Initialize program counter variable.
    fscanf(PROGRAM_COUNTER_FILE, "%x\t%*s\t%*s\t%*s", &PROGRAM_COUNTER);

    return;
}

int increment_pc()
{
    if (!feof(PROGRAM_COUNTER_FILE))
    {
        fscanf(PROGRAM_COUNTER_FILE, "%x\t%*s\t%*s\t%*s\n", &PROGRAM_COUNTER);
        return 1;
    }
    else
        return 0;
}

int get_object_code_length(unsigned long long int assembled_object_code)
{
    // get the length of the object code,
    // just take log to the base 16, which gives the number of hexadecimal digits.
    // then divide by 2, since each hexadecimal digit represents a nibble,
    // therefore 2 nibbles make a byte;
    // then take the ceiling function to get length.
    // There will always be an even number of hexadecimal digits.
    // Therefore, when divided by 2, results will be have odd and even values,
    // which is exactly what we expect

    // By change of base formula,
    // log16(x) = log2(x)/log2(16) = log2(x) / 4

    return (int)ceil(log2(assembled_object_code) / 4.0 / 2.0);
}

unsigned long long int get_string_literal_hex(char operand_without_extraneous[])
{
    // convert string to ascii value string (as an unsigned long int) and return.
    unsigned long long int obj_code = 0;

    for (int i = 0; i < strlen(operand_without_extraneous); i++)
    {
        obj_code <<= 8;
        obj_code += operand_without_extraneous[i];
    }

    return obj_code;
}

void get_literal_value(char operand_without_extraneous[], char operand[])
{
    // strip the apostropes and the first character
    // 'C' or 'X' and return the remaining string.
    // We use strncpy.

    strncpy(operand_without_extraneous, operand + 2, strlen(operand) - 3);
    operand_without_extraneous[strlen(operand) - 3] = '\0';
    return;
}

void update_text_record_length(FILE *temp_text_record, int text_record_length, int num_instructions)
{
    // Text record length is in bytes, two hex digits make a byte.
    // But each character stored in a file is takes up a byte.

    // Therefore we have to multiply text_record_length by 2 to get to
    // the column where length is stored.

    // File points to the place after the '\n' character and therefore
    // Minus one gets to '\n', and minus 2 gets to the last character of the text record.
    // Think of it as indexing starting from 2, instead of 0,

    // Then we have to index to the Nth character if indexed from 0,
    // But here index is from 2, so we have to index to (N+2) th character

    // Example, if the record length (in characters) 60, then we have to index to 62nd
    // Character from last (since 2 bytes is for text-record-length columns)

    // Therefore we have to index to -(60 + 2 + 2)th index.

    // Now that we have added spaces. We need to also add number of spaces i.e.,
    // number of object code instructions to the offset.
    fseek(temp_text_record, -2 * (text_record_length)-2 - 2 - num_instructions, SEEK_CUR);
    fprintf(temp_text_record, "%02x", text_record_length);

    // Go back to the end of the file.
    fseek(temp_text_record, 0, SEEK_END);

    return;
}

void update_text_record_start_address(FILE *temp_text_record, int text_record_start_address, int text_record_length)
{
    fprintf(temp_text_record, "%c %06x %02x", 'T', text_record_start_address, text_record_length);
    return;
}