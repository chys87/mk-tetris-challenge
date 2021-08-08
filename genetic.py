#!/usr/bin/env python3


import json
import re
import subprocess
import tempfile
import time


class Gene:
    __slots__ = ('bits', 'name', 'calc', 'initial')

    def __init__(self, bits, initial, name, calc):
        self.bits = bits
        self.initial = initial
        self.name = name
        self.calc = calc


def float_range_calc(lo, hi):
    def func(x, bits):
        return '{:.3f}'.format(lo + (hi - lo) * x / (1 << bits))
    return func


def int_range_calc(lo, hi):
    def func(x, bits):
        return str(lo + (hi - lo) * x // (1 << bits))
    return func


def choice_calc(choices):
    def func(x, bits):
        return choices[x % (1 << bits)]
    return func


GENE_LIST = [
    # 从已知的 1394874 开始搜索:
    # --total_keep=9041 --score_keep_ratio=0.163 --score_height_quota=0.210
    # --quality_height_quota=0.355 --score_parent_quota=0.3,0.5,0.7,0.9
    # --quality_parent_quota=0.3,0.5,0.7,0.9 --ignore_score_threshold=2200
    # --ignore_height_threshold=6 --quality_row_transition_penalty=448
    # --quality_col_transition_penalty=0 --quality_empty_penalty=1080
    Gene(2, 0, 'total_keep', int_range_calc(9041, 10000)),
    Gene(4, 8, 'score_keep_ratio', float_range_calc(0.153, 0.173)),
    Gene(4, 8, 'score_height_quota', float_range_calc(0.20, 0.22)),
    Gene(4, 8, 'quality_height_quota', float_range_calc(0.345, 0.365)),
    Gene(4, 8, 'ignore_score_threshold', int_range_calc(2100, 2300)),
    Gene(4, 8, 'quality_row_transition_penalty', int_range_calc(428, 468)),
    Gene(4, 0, 'quality_col_transition_penalty', int_range_calc(0, 200)),
    Gene(4, 8, 'quality_empty_penalty', int_range_calc(1030, 1130)),
    Gene(4, 0, 'quality_empty_penalty2', int_range_calc(0, 100)),
]

GENOME_BITS = sum(x.bits for x in GENE_LIST)


def genome_to_params(genome):
    assert(len(genome) == GENOME_BITS)
    params = []
    off = 0
    for gene in GENE_LIST:
        binary_value = int(genome[off:off+gene.bits], 2)
        real_value = gene.calc(binary_value, gene.bits)
        params.append('--{}={}'.format(gene.name, real_value))
        off += gene.bits
    return ' '.join(params)


def rand(*, context=[12358]):
    context[0] = (context[0] * 27073 + 17713) % 32749
    return context[0]


def not_(s):
    if s == '0':
        return '1'
    else:
        return '0'


class Running:
    def __init__(self, genome, str_params, abort_threshold):
        self.genome = genome
        self.str_params = str_params
        self.abort_threshold = abort_threshold
        self.stdout = tempfile.NamedTemporaryFile()
        self.stderr = tempfile.NamedTemporaryFile()

        str_threshold = ','.join(map(str, abort_threshold))
        self.proc = subprocess.Popen(
                ['./main'] + str_params.split(' ') +
                ['--abort_threshold={}'.format(str_threshold)],
                stdout=self.stdout, stderr=self.stderr)
        self.result = None

    def poll(self):
        if self.result is not None:
            return self.result
        if self.proc.poll() is None:
            return None
        self.proc.wait()
        self.stdout.seek(0)
        out = self.stdout.read()

        m = re.search(br'final_score=(\d+)', out)
        final_score = int(m.group(1))

        m = re.search(br'score_by_step=([\d,]*)', out)
        score_by_step = [int(x) for x in m.group(1).split(b',') if x]
        self.result = final_score, score_by_step
        return self.result


class State:
    _cache_file = 'out/genetic.cache'
    _max_parallel = 8

    def __init__(self):
        self.done_genomes = {}  # genome -> str_params
        self.results = {}  # str_params -> (score, score_by_step)
        self.running = []  # type: list[Running]
        self.pending = self.get_initial_genomes()  # type: list[str]

        self.load_cache()

    def get_initial_genomes(self):
        initial_gnome = ''
        for gene in GENE_LIST:
            initial = gene.initial
            bits = gene.bits
            initial_gnome += '{:0{}b}'.format(initial, bits)
        res = [initial_gnome]
        for i in range(GENOME_BITS):
            res.append(initial_gnome[:i] + not_(initial_gnome[i]) +
                       initial_gnome[i+1:])
        return res

    def load_cache(self):
        try:
            f = open(self._cache_file, 'r')
        except FileNotFoundError:
            return
        with f:
            self.done_genomes, results = json.load(f)
        for str_params, (score, score_by_step) in results.items():
            self.results[str_params] = score, score_by_step

    def save_cache(self):
        with open(self._cache_file, 'w') as f:
            json.dump((self.done_genomes, self.results), f)

    def get_best(self):
        '''Returns genome, (score, score_by_step) or None'''
        best = None
        for genome, str_params in self.done_genomes.items():
            score, score_by_step = self.results[str_params]
            if best is None or score > best[1][0]:
                best = genome, (score, score_by_step)
        return best

    def get_bests(self, n):
        lst = [(genome, self.results[str_params])
               for (genome, str_params) in self.done_genomes.items()]
        lst.sort(key=lambda x: x[1][0], reverse=True)
        return lst[:n]

    def get_next_genome(self):
        if self.pending:
            res = self.pending[0]
            self.pending[:1] = []
            return res

        best_genomes = [
            (genome, self.results[str_params][0])
            for (genome, str_params) in self.done_genomes.items()
            if self.results[str_params][0] > 0]
        best_genomes.sort(key=lambda x: x[1], reverse=True)
        best_genomes[8:] = []

        # 变异
        v, _ = best_genomes[rand() % len(best_genomes)]
        for _ in range(3):
            j = rand() % GENOME_BITS
            v = v[:j] + not_(v[j]) + v[j+1:]
        self.pending.append(v)

        # 杂交
        u, _ = best_genomes[rand() % len(best_genomes)]
        v, _ = best_genomes[rand() % len(best_genomes)]
        if u != v:
            i = rand() % GENOME_BITS
            j = rand() % GENOME_BITS
            i, j = min(i, j), max(i, j)
            if i < j:
                self.pending.append(u[:i] + v[i:j+1] + u[j+1:])
                self.pending.append(v[:i] + u[i:j+1] + v[j+1:])

        return self.pending.pop()

    def run_new(self):
        while True:
            genome = self.get_next_genome()
            if genome in self.done_genomes:
                print('Already done {}'.format(genome))
                continue
            if any(r.genome == genome for r in self.running):
                print('Already running {}'.format(genome))
                continue
            str_params = genome_to_params(genome)
            if str_params in self.results:
                self.done_genomes[genome] = str_params
                print('Hit cache {}'.format(genome))
                continue

            bests = self.get_bests(20)
            if len(bests) >= 20:
                abort_threshold = [max(x - 1000, 0) for x in bests[19][1][1]]
            else:
                abort_threshold = []
            print('Starting {}'.format(genome))
            r = Running(genome, genome_to_params(genome), abort_threshold)
            self.running.append(r)
            return

    def ping(self):
        for i in range(len(self.running) - 1, -1, -1):
            r = self.running[i]
            result = r.poll()
            if result:
                print('Done {} {}'.format(r.genome, result[0]))
                with open('out/genetic.log', 'a') as f:
                    print('{} {}'.format(result[0], r.str_params), file=f)
                self.results[r.str_params] = result
                self.done_genomes[r.genome] = r.str_params
                self.save_cache()
                self.running[i:i+1] = []

        while len(self.running) < self._max_parallel:
            self.run_new()

        best = self.get_best()
        if best:
            print('Current best: {} {}'.format(best[0], best[1][0]))


def main():
    state = State()

    while True:
        state.ping()
        time.sleep(1)


if __name__ == '__main__':
    main()
