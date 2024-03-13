import os
directory = 'log/'

from dataclasses import dataclass

@dataclass
class LogItem:
    malloc_count: int
    free_count: int

    alloc_p50: int
    alloc_p90: int
    alloc_p99: int
    alloc_worst: int
    
    free_p50: int
    free_p90: int
    free_p99: int
    free_worst: int
 
res = dict()
for filename in os.listdir(directory):
    f = os.path.join(directory, filename)
    # if os.path.isfile(f):
    #     print(f)
    
    with open(f) as log_file:
        # split filename
        f = f.replace(directory, "").split(".")
        print(f)
        elf_name = f[0]
        benchmark_type = f[1]
        benchmark_run = f[2]

        # parse data
        str = log_file.readline()
        str = str.replace("\n", "").replace("ns", "").split(",")
        print(str)

        """
        malloc count, free count, alloc p50, p90, p99, worst, free p50, p90, p99, worst
        """

        # store data
        if elf_name not in res:
            res[elf_name] = dict()
        if benchmark_type not in res[elf_name]:
            res[elf_name][benchmark_type] = list()
        res[elf_name][benchmark_type].append(LogItem(
            malloc_count=str[0],
            free_count=str[1],

            alloc_p50=str[2],
            alloc_p90=str[3],
            alloc_p99=str[4],
            alloc_worst=str[5],
            
            free_p50=str[6],
            free_p90=str[7],
            free_p99=str[8],
            free_worst=str[9],
        ))

print(res)

# analyze across run
