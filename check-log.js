const fs = require("node:fs");
const path = require("node:path");

const log = fs.readFileSync(path.resolve('D:\\', 'log.txt'), {
  encoding: "utf8",
  flag: "rs",
});

const usedFuncs = [...new Set(log.split('\n').filter(e => e.startsWith('[')).map(e => e.split(' ')[0].replace(/[\[\]]/g, '')))];

usedFuncs.forEach(e => console.log(e));
