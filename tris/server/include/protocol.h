#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ================================================================== */
/*  PROTOCOL.H  –  Costanti del protocollo client-server Tris          */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Benvenuto                                                           */
/* ------------------------------------------------------------------ */
#define PROTO_WELCOME           "WELCOME\n"
#define PROTO_HINT_LOGIN        "Please LOGIN <n>\n"
#define PROTO_HINT_CMDS         "Commands: LOGIN <n>, WHOAMI, USERS, CREATE, LIST, " \
                                "JOIN <id>, ACCEPT <id>, REJECT <id>, "                \
                                "MOVE <r> <c>, BOARD, RESIGN, REMATCH, QUIT\n"

/* ------------------------------------------------------------------ */
/*  Login                                                               */
/* ------------------------------------------------------------------ */
#define PROTO_OK_LOGIN          "OK LOGIN %s\n"
#define PROTO_ERR_NAME_TAKEN    "ERR NAME_TAKEN\n"
#define PROTO_ERR_BAD_NAME      "ERR BAD_NAME\n"
#define PROTO_ERR_PLEASE_LOGIN  "ERR PLEASE_LOGIN\n"

/* ------------------------------------------------------------------ */
/*  Comandi generici                                                    */
/* ------------------------------------------------------------------ */
#define PROTO_OK_WHOAMI        "OK YOU %s\n"
#define PROTO_NO_USERS         "NO_USERS\n"
#define PROTO_BYE              "BYE\n"
#define PROTO_ERR_UNKNOWN_CMD  "ERR UNKNOWN_CMD\n"
#define PROTO_ERR_BAD_USAGE    "ERR BAD_USAGE\n"

/* ------------------------------------------------------------------ */
/*  CREATE / LIST                                                       */
/* ------------------------------------------------------------------ */
#define PROTO_OK_MATCH_CREATED  "OK MATCH_CREATED %d\n"
#define PROTO_ERR_MATCHES_FULL  "ERR MATCHES_FULL\n"
#define PROTO_NO_MATCHES        "NO_MATCHES\n"

/* ------------------------------------------------------------------ */
/*  JOIN                                                                */
/* ------------------------------------------------------------------ */
#define PROTO_OK_JOIN_REQUESTED       "OK JOIN_REQUESTED\n"
#define PROTO_ERR_MATCH_NOT_FOUND     "ERR MATCH_NOT_FOUND\n"
#define PROTO_ERR_MATCH_NOT_JOINABLE  "ERR MATCH_NOT_JOINABLE\n"
#define PROTO_ERR_CANNOT_JOIN_OWN     "ERR CANNOT_JOIN_OWN_MATCH\n"
#define PROTO_ERR_JOIN_FAILED         "ERR JOIN_FAILED\n"
#define PROTO_ERR_ALREADY_PLAYING     "ERR ALREADY_PLAYING\n"
#define PROTO_EVENT_JOIN_REQUEST      "EVENT JOIN_REQUEST %d %s\n"

/* ------------------------------------------------------------------ */
/*  ACCEPT / REJECT                                                     */
/* ------------------------------------------------------------------ */
#define PROTO_OK_MATCH_STARTED_X  "OK MATCH_STARTED %d vs %s (YOU=X)\n"
#define PROTO_OK_MATCH_STARTED_O  "OK MATCH_STARTED %d vs %s (YOU=O)\n"
#define PROTO_ERR_NOT_OWNER       "ERR NOT_OWNER\n"
#define PROTO_ERR_NO_PENDING      "ERR NO_PENDING_REQUEST\n"
#define PROTO_ERR_ACCEPT_FAILED   "ERR ACCEPT_FAILED\n"
#define PROTO_OK_REJECTED         "OK REJECTED\n"
#define PROTO_ERR_REJECT_FAILED   "ERR REJECT_FAILED\n"
#define PROTO_ERR_JOIN_REJECTED   "ERR JOIN_REJECTED\n"

/* ------------------------------------------------------------------ */
/*  MOVE                                                                */
/* ------------------------------------------------------------------ */
#define PROTO_OK_MOVED               "OK MOVED\n"
#define PROTO_ERR_NOT_IN_MATCH       "ERR NOT_IN_MATCH\n"
#define PROTO_ERR_NOT_YOUR_TURN      "ERR NOT_YOUR_TURN\n"
#define PROTO_ERR_BAD_MOVE           "ERR BAD_MOVE\n"
#define PROTO_ERR_MATCH_NOT_PLAYING  "ERR MATCH_NOT_PLAYING\n"
#define PROTO_ERR_MOVE_FAILED        "ERR MOVE_FAILED\n"
#define PROTO_EVENT_OPPONENT_MOVED   "EVENT OPPONENT_MOVED %d %d\n"

/* ------------------------------------------------------------------ */
/*  Fine partita                                                        */
/* ------------------------------------------------------------------ */
#define PROTO_EVENT_YOU_WIN   "EVENT YOU_WIN\n"
#define PROTO_EVENT_YOU_LOSE  "EVENT YOU_LOSE\n"
#define PROTO_EVENT_DRAW      "EVENT DRAW\n"
#define PROTO_EVENT_WINNER    "EVENT WINNER %s\n"
/*
 * Inviato al termine della partita:
 * - al vincitore (o a entrambi in caso di pareggio): può fare REMATCH
 * - al perdente: può solo fare CREATE/JOIN su altre partite
 */
#define PROTO_EVENT_GAME_OVER_WIN   "EVENT GAME_OVER Hai vinto! Digita REMATCH per aprire una nuova partita, QUIT per uscire\n"
#define PROTO_EVENT_GAME_OVER_LOSE  "EVENT GAME_OVER Hai perso. Digita CREATE o JOIN per una nuova partita, QUIT per uscire\n"
#define PROTO_EVENT_GAME_OVER_DRAW  "EVENT GAME_OVER Pareggio! Digita REMATCH per aprire una nuova partita, QUIT per uscire\n"

/* ------------------------------------------------------------------ */
/*  RESIGN                                                              */
/* ------------------------------------------------------------------ */
#define PROTO_ERR_NO_OPPONENT   "ERR NO_OPPONENT\n"
#define PROTO_ERR_RESIGN_FAILED "ERR RESIGN_FAILED\n"

/* ------------------------------------------------------------------ */
/*  BOARD                                                               */
/* ------------------------------------------------------------------ */
#define PROTO_ERR_BOARD_NOT_FOUND "ERR MATCH_NOT_FOUND\n"

/* ------------------------------------------------------------------ */
/*  REMATCH                                                             */
/*                                                                      */
/*  Logica conforme alla traccia:                                       */
/*   - Vincitore fa REMATCH → crea partita WAITING come owner (X)      */
/*     Broadcast a tutti → chiunque può fare JOIN                       */
/*   - Pareggio → entrambi possono fare REMATCH (stesso comportamento)  */
/*   - Perdente → ERR_REMATCH_DENIED, può solo CREATE/JOIN              */
/* ------------------------------------------------------------------ */
/* Conferma al vincitore/pareggiante: nuova partita creata */
#define PROTO_OK_REMATCH_CREATED   "OK REMATCH_CREATED %d Sei owner (X), attendi un avversario\n"
/* Errori */
#define PROTO_ERR_REMATCH_DENIED   "ERR REMATCH_DENIED Hai perso, non puoi richiedere la rivincita. Usa CREATE o JOIN\n"
#define PROTO_ERR_REMATCH_FAILED   "ERR REMATCH_FAILED\n"
#define PROTO_ERR_REMATCH_NOT_AVAIL "ERR REMATCH_NOT_AVAILABLE Non sei in una partita terminata\n"

/* ------------------------------------------------------------------ */
/*  Disconnect / eventi asincroni                                       */
/* ------------------------------------------------------------------ */
#define PROTO_EVENT_OPP_DISCONNECTED "EVENT OPPONENT_DISCONNECTED\n"
#define PROTO_ERR_MATCH_CLOSED       "ERR MATCH_CLOSED_OWNER_LEFT\n"

/* ------------------------------------------------------------------ */
/*  Broadcast cambio stato partita (a tutti i client connessi)         */
/* ------------------------------------------------------------------ */
#define PROTO_EVENT_MATCH_AVAILABLE   "EVENT MATCH_AVAILABLE %d owner=%s\n"
#define PROTO_EVENT_MATCH_STARTED_ALL "EVENT MATCH_STARTED %d\n"
#define PROTO_EVENT_MATCH_FINISHED    "EVENT MATCH_FINISHED %d\n"

/* ------------------------------------------------------------------ */
/*  Helper: invia messaggio formattato (printf-style) su un fd         */
/* ------------------------------------------------------------------ */
void proto_sendf(int fd, const char *fmt, ...);

#endif 