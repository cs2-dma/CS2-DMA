'use strict';

const fs = require('fs');
const path = require('path');

const projectRoot = path.resolve(__dirname, '..', '..');
const localeDirectory = path.join(projectRoot, 'src', 'assets', 'locales');
const grenadeHelperDatabase = path.join(
    projectRoot,
    'src',
    'assets',
    'world',
    'grenade_helper.json');
const sourceExtensions = new Set([
    '.cc',
    '.cpp',
    '.cxx',
    '.h',
    '.hpp',
    '.html',
    '.inl',
    '.js',
    '.rc'
]);

function readCatalog(fileName) {
    const text = fs.readFileSync(path.join(localeDirectory, fileName), 'utf8');
    return { text, json: JSON.parse(text) };
}

function duplicateTranslationKeys(text) {
    const keys = [];
    const keyPattern = /^\s{4}"((?:\\.|[^"\\])*)"\s*:/gm;
    for (const match of text.matchAll(keyPattern)) {
        const decoded = decodeCppString(match[1]);
        if (decoded !== null)
            keys.push(decoded);
    }

    const seen = new Set();
    const duplicates = new Set();
    for (const key of keys) {
        if (seen.has(key))
            duplicates.add(key);
        seen.add(key);
    }
    return [...duplicates];
}

function shouldSkip(relativePath) {
    const normalized = relativePath.replaceAll('\\', '/');
    if (normalized === '.git' || normalized.startsWith('.git/'))
        return true;
    if (normalized === 'x64' || normalized.startsWith('x64/'))
        return true;
    if (normalized.startsWith('src/vendor/') &&
        normalized !== 'src/vendor/DMALibrary/pch.cpp') {
        return true;
    }
    return false;
}

function collectSourceFiles(directory, output) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const absolutePath = path.join(directory, entry.name);
        const relativePath = path.relative(projectRoot, absolutePath);
        if (shouldSkip(relativePath))
            continue;
        if (entry.isDirectory()) {
            collectSourceFiles(absolutePath, output);
        } else if (sourceExtensions.has(path.extname(entry.name).toLowerCase())) {
            output.push(relativePath);
        }
    }
}

function decodeCppString(value) {
    try {
        return JSON.parse(`"${value}"`);
    } catch {
        return null;
    }
}

function normalizeDmaFormat(value) {
    return value
        .replace(/^\[(?:ERROR|WARN|INFO|DEBUG|PERF)\]/, '')
        .replace(/^\[(?:!|-|\+)\]/, '')
        .trim()
        .replace(/[\r\n]+$/, '');
}

function printfSignature(value) {
    const result = [];
    const pattern =
        /%(?:\d+\$)?[-+ #0']*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:I64|I32|hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn]/g;
    for (const match of value.matchAll(pattern)) {
        if (match[0] !== '%%')
            result.push(match[0].replace(/^%(?:\d+\$)?/, '%'));
    }
    return result.join('|');
}

function replacementCount(value) {
    let count = 0;
    for (let index = 0; index < value.length; ++index) {
        if (value[index] !== '{')
            continue;
        if (value[index + 1] === '{') {
            ++index;
            continue;
        }
        ++count;
    }
    return count;
}

function collectCaseInsensitiveDuplicates(keys) {
    const firstByFoldedKey = new Map();
    const duplicates = [];
    for (const key of keys) {
        const folded = key.toLocaleLowerCase('en-US');
        const first = firstByFoldedKey.get(folded);
        if (first && first !== key)
            duplicates.push([first, key]);
        else
            firstByFoldedKey.set(folded, key);
    }
    return duplicates;
}

const englishCatalog = readCatalog('en.json');
const chineseCatalog = readCatalog('zh-CN.json');
const english = englishCatalog.json.translations;
const chinese = chineseCatalog.json.translations;
const englishKeys = Object.keys(english);
const chineseKeys = Object.keys(chinese);
const grenadeDatabase = JSON.parse(fs.readFileSync(grenadeHelperDatabase, 'utf8'));
const grenadeCalloutKeys = new Set();
for (const map of grenadeDatabase.maps ?? []) {
    for (const spot of map.spots ?? []) {
        for (const aim of spot.aim_points ?? []) {
            if (typeof aim.label !== 'string')
                continue;
            for (const callout of aim.label.split(' From ', 2))
                grenadeCalloutKeys.add(`Grenade callout: ${callout}`);
        }
    }
}

const errors = [];
const warnings = [];
const duplicateEnglish = duplicateTranslationKeys(englishCatalog.text);
const duplicateChinese = duplicateTranslationKeys(chineseCatalog.text);
if (duplicateEnglish.length)
    errors.push(`Duplicate en keys: ${duplicateEnglish.join(', ')}`);
if (duplicateChinese.length)
    errors.push(`Duplicate zh-CN keys: ${duplicateChinese.join(', ')}`);
const missingChinese = englishKeys.filter((key) => !(key in chinese));
const extraChinese = chineseKeys.filter(
    (key) => !(key in english) && !grenadeCalloutKeys.has(key));
if (missingChinese.length)
    errors.push(`Missing zh-CN keys: ${missingChinese.join(', ')}`);
if (extraChinese.length)
    errors.push(`Extra zh-CN keys: ${extraChinese.join(', ')}`);

const grenadeThrowTypes = new Set();
for (const map of grenadeDatabase.maps ?? []) {
    for (const spot of map.spots ?? []) {
        for (const aim of spot.aim_points ?? []) {
            if (typeof aim.throw_type === 'string' && aim.throw_type)
                grenadeThrowTypes.add(aim.throw_type);
        }
    }
}
for (const throwType of grenadeThrowTypes) {
    if (!(throwType in english) || !(throwType in chinese))
        errors.push(`Missing Grenade Helper throw translation: ${throwType}`);
}

for (const key of englishKeys) {
    if (!(key in chinese))
        continue;
    if (typeof english[key] !== 'string' || !english[key])
        errors.push(`Invalid English value: ${key}`);
    if (typeof chinese[key] !== 'string' || !chinese[key])
        errors.push(`Invalid zh-CN value: ${key}`);
    if (printfSignature(key) !== printfSignature(chinese[key]) ||
        replacementCount(key) !== replacementCount(chinese[key])) {
        errors.push(`Incompatible format arguments: ${key}`);
    }
}

const sourceFiles = [];
collectSourceFiles(projectRoot, sourceFiles);
const sources = sourceFiles.map((fileName) => ({
    fileName,
    text: fs.readFileSync(path.join(projectRoot, fileName), 'utf8')
}));

const literalCallPatterns = [
    {
        pattern:
            /(?:KEVQ_TR|TranslateLogText|(?:app::)?localization::(?:Get|GetCopy|Format))\s*\(\s*"((?:\\.|[^"\\])*)"/g,
        normalize: (value) => value
    },
    {
        pattern:
            /(?:SetStatus|SectionTitle|FullButton|CompactButton)\s*\(\s*"((?:\\.|[^"\\])*)"/g,
        normalize: (value) => value
    },
    {
        pattern:
            /(?:DmaLogPrintf|DmaLogWPrintf)\s*\(\s*(?:L)?"((?:\\.|[^"\\])*)"/g,
        normalize: normalizeDmaFormat
    },
    {
        pattern:
            /\btr\s*\(\s*"((?:\\.|[^"\\])*)"/g,
        normalize: (value) => value
    },
    {
        pattern:
            /data-i18n(?:-aria-label|-alt)?="([^"]+)"/g,
        normalize: (value) => value
    }
];

const literalCalls = [];
for (const source of sources) {
    for (const descriptor of literalCallPatterns) {
        for (const match of source.text.matchAll(descriptor.pattern)) {
            const decoded = decodeCppString(match[1]);
            if (decoded !== null) {
                literalCalls.push({
                    fileName: source.fileName,
                    key: descriptor.normalize(decoded)
                });
            }
        }
    }
}

const missingCallKeys = literalCalls.filter(
    (call) =>
        !(call.key in english) &&
        !(grenadeCalloutKeys.has(call.key) && call.key in chinese));
for (const call of missingCallKeys)
    errors.push(`Missing catalog key "${call.key}" used by ${call.fileName}`);

const caseDuplicates = collectCaseInsensitiveDuplicates([
    ...new Set([...englishKeys, ...chineseKeys])
]);
const allowedCaseVariants = new Set([
    'Connected/connected',
    'Degraded/degraded',
    'Unknown/unknown',
    'WEBRadar/WebRadar'
]);
const unexpectedCaseDuplicates = caseDuplicates.filter(
    ([left, right]) => !allowedCaseVariants.has(`${left}/${right}`)
);
if (unexpectedCaseDuplicates.length) {
    warnings.push(
        `Case-sensitive variants: ${unexpectedCaseDuplicates
            .map(([left, right]) => `${left}/${right}`)
            .join(', ')}`);
}

for (const warning of warnings)
    console.warn(`localization audit warning: ${warning}`);
for (const error of errors)
    console.error(`localization audit error: ${error}`);

if (errors.length)
    process.exitCode = 1;
else {
    console.log(
        `localization audit passed: ${englishKeys.length} keys, ` +
        `${literalCalls.length} literal call sites, ${sourceFiles.length} source files`);
}
