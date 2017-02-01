#include <stdio.h>

int decode(unsigned int buff);

int main()
{
    FILE *iImage, *dImage, *result, *error;
    int iMemory[256]={0}, dMemory[256]={0}, reg[32]={0}, PC=0, iTotal=0, dTotal=0, cycle=0;
    int opcode, rs, rt, rd, shamt, funct, imm, immu, immj, addr, i, j, temp, temp2;
    int write_zero, number_overflow, address_overflow, data_misaligned;
    unsigned int buff;

    iImage = fopen("iimage.bin", "rb"); //read binary
    dImage = fopen("dimage.bin", "rb");
    result = fopen("snapshot.rpt", "w"); //file name is important!
    error = fopen("error_dump.rpt", "w");

    if(fread(&buff, sizeof(int), 1, iImage)) PC = decode(buff)/4; //PC, divided by 4 to get word unit

    if(fread(&buff, sizeof(int), 1, dImage)) reg[29] = decode(buff); //SP

    if(fread(&buff, sizeof(int), 1, iImage)) iTotal = decode(buff); //number of i instruction

    if(fread(&buff, sizeof(int), 1, dImage)) dTotal = decode(buff); //number of d instruction

    //there is 256 word blocks, putting instruction from PC and the length is iTotal
    for(i=PC; i<PC+iTotal; i++) if(fread(&buff, sizeof(int), 1, iImage)) iMemory[i] = decode(buff);

    //dMemory start from 0 block
    for(i=0; i<dTotal; i++) if(fread(&buff, sizeof(int), 1, dImage)) dMemory[i] = decode(buff);

    fclose(iImage); //remember to close the file
    fclose(dImage);

    while(1)
    {
        //print the information of the last cycle
        fprintf(result, "cycle %d\n", cycle); //cycle=0 at beginning
        for(i=0; i<32; i++) fprintf(result, "$%02d: 0x%08X\n", i, reg[i]); //print registers to snapshot
        fprintf(result, "PC: 0x%08X\n\n\n", PC*4); //My PC is in word unit, so transform it to byte unit when print

        //get fragments of instruction
        opcode = (unsigned int)iMemory[PC]>>26; //shift is convenient to get any part you want, unsigned can prevent automatically padded 1 bits when shift right
        rs = iMemory[PC]>>21&0x1F; //& is also good to get the part you want
        rt = iMemory[PC]>>16&0x1F; //remember shift is faster then &, so if you & first, you need to use ( )
        rd = iMemory[PC]>>11&0x1F;
        shamt = iMemory[PC]>>6&0x1F;
        funct = iMemory[PC]&0x3F;
        imm = iMemory[PC]<<16>>16; //sign immediate
        immu = iMemory[PC]&0xFFFF; //unsigned immediate
        immj = iMemory[PC]&0x03FFFFFF;

        //set four error to 0
        write_zero = 0;
        number_overflow = 0;
        address_overflow = 0;
        data_misaligned = 0;

        //add PC and cycle because the value of PC and cycle now is the last cycle's value
        PC++;
        cycle++;

        if(opcode==0x00) //R type
        {
            if(rd==0) write_zero = 1;

            if(funct < 10) //sll, srl, sra, jr
            {
                if(funct==0x00) //sll
                {
                    if(rd==0 && rt==0 && shamt==0) write_zero = 0; //NOP
                    if(!write_zero) reg[rd] = reg[rt] << shamt; //NOP do this is okay, reg[0] = reg[0]<<0, is still 0
                }

                else if(funct==0x02) //srl
                {
                    if(!write_zero) reg[rd] = (unsigned int)reg[rt] >> shamt; //unsigned to prevent automatically padded 1 bits
                }

                else if(funct==0x03) //sra
                {
                    if(!write_zero) reg[rd] = reg[rt] >> shamt; //automatically padded 1 bits if the number is negative
                }

                else //jr
                {
                    write_zero = 0; //jr won't cause write zero error
                    PC = reg[rs]/4;
                }
            }

            else if(funct < 35) //add, addu, sub
            {
                if(funct==0x20) //add
                {
                    temp = reg[rs] + reg[rt];
                    if(reg[rs]>0 && reg[rt]>0 && temp<=0) number_overflow = 1;
                    else if(reg[rs]<0 && reg[rt]<0 && temp>=0) number_overflow = 1;
                    if(!write_zero) reg[rd] = temp;
                }

                else if(funct==0x21) //addu
                {
                    if(!write_zero) reg[rd] = reg[rs] + reg[rt];
                }

                else //sub
                {
                    //the implementation of sub in MIPS is to use 2's complement and add
                    //if you use sub directly, you will be wrong with a special case of overflow:
                    //0x80000000, its 2's complement is still 0x80000000
                    temp2 = (~reg[rt])+1;
                    temp = reg[rs] + temp2;
                    if(reg[rs]>0 && temp2>0 && temp<=0) number_overflow = 1;
                    else if(reg[rs]<0 && temp2<0 && temp>=0) number_overflow = 1;
                    if(!write_zero) reg[rd] = temp;
                }
            }

            else //and, or, xor, nor, nand, slt
            {
                if(!write_zero)
                {
                    if(funct==0x24) reg[rd] = reg[rs] & reg[rt]; //and
                    else if(funct==0x25) reg[rd] = reg[rs] | reg[rt]; //or
                    else if(funct==0x26) reg[rd] = reg[rs] ^ reg[rt]; //xor
                    else if(funct==0x27) reg[rd] = ~(reg[rs] | reg[rt]); //nor
                    else if(funct==0x28) reg[rd] = ~(reg[rs] & reg[rt]); //nand
                    else reg[rd] = reg[rs] < reg[rt]; //slt
                }
            }

        }

        else //I, J, S type
        {
            if(opcode < 8) //j, jal, beq, bne, bgtz
            {
                if(opcode==0x02) //j
                {
                    PC = immj;
                    //PC is at most 1024 bytes, so directly take C's value is okay
                    //C's value is in word unit, and my PC is also in word unit, so doesn't need to *4
                }

                else if(opcode==0x03) //jal
                {
                    reg[31] = PC*4; //transform PC from word to byte, and PC is already +1, so directly *4
                    PC = immj; //same as j
                }

                else if(opcode==0x04) //beq
                {
                    if(reg[rs] == reg[rt]) PC = PC + imm;
                }

                else if(opcode==0x05) //bne
                {
                    if(reg[rs] != reg[rt]) PC = PC + imm;
                }

                else //bgtz
                {
                    if(reg[rs] > 0) PC = PC + imm;
                }
            }

            else if(opcode < 10) //addi, addiu
            {
                if(rt==0) write_zero = 1;

                if(opcode==0x08) //addi
                {
                    temp = reg[rs] + imm;
                    if(reg[rs]>0 && imm>0 && temp<=0) number_overflow = 1;
                    else if(reg[rs]<0 && imm<0 && temp>=0) number_overflow = 1;
                    if(!write_zero) reg[rt] = temp;
                }

                else //addiu
                {
                    if(!write_zero) reg[rt] = reg[rs] + imm;
                }
            }

            else if(opcode < 20) //slti, andi, ori, nori, lui
            {
                if(rt==0) write_zero = 1;
                else
                {
                    if(opcode==0x0A) reg[rt] = reg[rs] < imm; //slti
                    else if(opcode==0x0C) reg[rt] = reg[rs] & immu; //andi
                    else if(opcode==0x0D) reg[rt] = reg[rs] | immu; //ori
                    else if(opcode==0x0E) reg[rt] = ~(reg[rs] | immu); //nori
                    else reg[rt] = imm << 16; //lui (the result of signed and unsigned is the same)
                }
            }

            else if(opcode < 40) //lb, lh, lw, lbu, lhu
            {
                if(rt==0) write_zero = 1;
                addr = reg[rs] + imm; //address to load(read) or save(write)
                if(reg[rs]>0 && imm>0 && addr<=0) number_overflow = 1;
                else if(reg[rs]<0 && imm<0 && addr>=0) number_overflow = 1;
                i = addr/4;
                j = addr%4;

                if(opcode==0x20) //lb
                {
                    if(addr<0 || addr>1023) address_overflow = 1;
                    if(!write_zero && !address_overflow)
                    {
                        if(j==3) reg[rt] = dMemory[i]<<24>>24; //get last byte(signed), shift can automatically deal with signed bit
                        else if(j==2) reg[rt] = dMemory[i]<<16>>24; //get byte 3
                        else if(j==1) reg[rt] = dMemory[i]<<8>>24; //get byte 2
                        else reg[rt] = dMemory[i]>>24; //get byte 1
                    }
                }

                else if(opcode==0x21) //lh
                {
                    if(addr<0 || addr>1022) address_overflow = 1;
                    if(addr%2!=0) data_misaligned = 1;
                    if(!write_zero && !address_overflow && !data_misaligned)
                    {
                        if(j==2) reg[rt] = dMemory[i]<<16>>16; //get byte 3 and 4
                        else reg[rt] = dMemory[i]>>16; //get byte 1 and 2
                    }
                }

                else if(opcode==0x23) //lw
                {
                    if(addr<0 || addr>1020) address_overflow = 1;
                    if(addr%4!=0) data_misaligned = 1;
                    if(!write_zero && !address_overflow && !data_misaligned) reg[rt] = dMemory[i]; //get a word
                }

                else if(opcode==0x24) //lbu
                {
                    if(addr<0 || addr>1023) address_overflow = 1;
                    if(!write_zero && !address_overflow)
                    {
                        if(j==3) reg[rt] = dMemory[i]&0xFF; //get byte 4(unsigned)
                        else if(j==2) reg[rt] = dMemory[i]>>8&0xFF; //get byte 3
                        else if(j==1) reg[rt] = dMemory[i]>>16&0xFF; //get byte 2
                        else reg[rt] = (unsigned int)dMemory[i]>>24; //get byte 1, use unsigned to prevent automatically padded 1 bit
                    }
                }

                else //lhu
                {
                    if(addr<0 || addr>1022) address_overflow = 1;
                    if(addr%2!=0) data_misaligned = 1;
                    if(!write_zero && !address_overflow && !data_misaligned)
                    {
                        if(j==2) reg[rt] = dMemory[i]&0xFFFF; //get byte 3 and 4 (unsigned)
                        else reg[rt] = (unsigned int)dMemory[i]>>16; //get byte 1 and 2
                    }
                }
            }

            else if(opcode < 50) //sb, sh, sw
            {
                addr = reg[rs] + imm; //address to load(read) or save(write)
                if(reg[rs]>0 && imm>0 && addr<=0) number_overflow = 1;
                else if(reg[rs]<0 && imm<0 && addr>=0) number_overflow = 1;
                i = addr/4;
                j = addr%4;

                if(opcode==0x28) //sb
                {
                    if(addr<0 || addr>1023) address_overflow = 1;
                    if(!address_overflow)
                    {
                        if(j==3)
                        {
                            dMemory[i] &= 0xFFFFFF00; //clear memory byte 4
                            dMemory[i] |= reg[rt]&0xFF; //save rt byte 4 to memory byte 4
                        }
                        else if(j==2)
                        {
                            dMemory[i] &= 0xFFFF00FF; //clear memory byte 3
                            dMemory[i] |= reg[rt]<<8&0xFF00; //save rt byte 4 to memory byte 3
                        }
                        else if(j==1)
                        {
                            dMemory[i] &= 0xFF00FFFF; //clear memory byte 2
                            dMemory[i] |= reg[rt]<<16&0xFF0000; //save rt byte 4 to memory byte 2
                        }
                        else
                        {
                            dMemory[i] &= 0xFFFFFF; //clear memory byte 1
                            dMemory[i] |= reg[rt]<<24; //save rt byte 4 to memory byte 1
                        }
                    }
                }

                else if(opcode==0x29) //sh
                {
                    if(addr<0 || addr>1022) address_overflow = 1;
                    if(addr%2!=0) data_misaligned = 1;
                    if(!address_overflow && !data_misaligned)
                    {
                        if(j==2)
                        {
                            dMemory[i] &= 0xFFFF0000; //clear memory byte 3 and 4
                            dMemory[i] |= reg[rt]&0xFFFF; //save rt byte 3 and 4 to memory byte 3 and 4
                        }
                        else
                        {
                            dMemory[i] &= 0xFFFF; //clear memory byte 1 and 2
                            dMemory[i] |= reg[rt]<<16; //save rt byte 3 and 4 to memory byte 1 and 2
                        }
                    }
                }

                else //sw
                {
                    if(addr<0 || addr>1020) address_overflow = 1;
                    if(addr%4!=0) data_misaligned = 1;
                    if(!address_overflow && !data_misaligned) dMemory[i] = reg[rt]; //save rt to memory
                }
            }

            else break; //halt
        }

        if(write_zero) fprintf(error, "In cycle %d: Write $0 Error\n", cycle);
        if(number_overflow) fprintf(error, "In cycle %d: Number Overflow\n", cycle);
        if(address_overflow) fprintf(error, "In cycle %d: Address Overflow\n", cycle);
        if(data_misaligned) fprintf(error, "In cycle %d: Misalignment Error\n", cycle);
        if(address_overflow || data_misaligned) break;
    }
    fclose(result); //remember to close file
    fclose(error);
    return 0;
}

int decode(unsigned int buff)
{
    //change the order of byte because of the different of big and little endian
    //one int has four bytes, the byte order originally is 1234, we need to change it to 4321
    //one byte has 8 bits, this transformation can be easily done by shifting
    int answer = 0;
    answer |= buff<<24; //change byte 4 to byte 1
    answer |= buff<<8&0xFF0000; //change byte 3 to byte 2
    answer |= buff>>8&0xFF00; //change byte 2 to byte 3
    answer |= buff>>24; //change byte 1 to byte 4
    return answer;
}
