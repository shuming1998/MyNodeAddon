/*
 * @Author: cuishuming@baidu.com
 * @Date: 2024-04-10 15:47:05
 * @LastEditors: cuishuming@baidu.com
 * @LastEditTime: 2024-04-10 15:47:23
 * @FilePath: /Desktop/MyNodeAddon/index.js
 * @Description: 
 * Copyright (c) 2024 by ${cuishuming@baidu.com}, All Rights Reserved. 
 */
const addon = require('./build/Release/mynodeaddon');

//console.time('createObjectFromMyStruct');
const myStructObj = addon.createObjectFromMyStruct();
//console.timeEnd('createObjectFromMyStruct');

//console.time('createObjectFromCppStruct');
const cppStructObj = addon.createObjectFromCppStruct();
//console.timeEnd('createObjectFromCppStruct');

console.log(myStructObj);
console.log(cppStructObj);