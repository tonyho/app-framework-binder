/*
 * Copyright 2016 IoT.bzh
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// User Client Session Context

struct AFB_clientCtx
{
  time_t timeStamp;     // last time token was refresh
  void **contexts;      // application specific context [one per plugin]]
  char uuid[37];        // long term authentication of remote client
  char token[37];       // short term authentication of remote client
};
typedef struct AFB_clientCtx AFB_clientCtx;

extern void ctxStoreGarbage ();

extern void ctxStoreInit (int nbSession, int timeout, int apicount, const char *initok);

extern AFB_clientCtx *ctxClientGet (const char *uuid);
extern int ctxClientClose (AFB_clientCtx *clientCtx);
extern int ctxTokenCheck (AFB_clientCtx *clientCtx, const char *token);
extern int ctxTokenNew (AFB_clientCtx *clientCtx);

