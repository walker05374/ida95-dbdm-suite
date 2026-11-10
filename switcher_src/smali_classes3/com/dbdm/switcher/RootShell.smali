.class public final Lcom/dbdm/switcher/RootShell;
.super Ljava/lang/Object;
.source "RootShell.kt"


# annotations
.annotation system Ldalvik/annotation/SourceDebugExtension;
    value = "SMAP\nRootShell.kt\nKotlin\n*S Kotlin\n*F\n+ 1 RootShell.kt\ncom/dbdm/switcher/RootShell\n+ 2 fake.kt\nkotlin/jvm/internal/FakeKt\n*L\n1#1,92:1\n1#2:93\n*E\n"
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000B\n\u0002\u0018\u0002\n\u0002\u0010\u0000\n\u0002\u0008\u0002\n\u0002\u0010\u000b\n\u0000\n\u0002\u0018\u0002\n\u0002\u0008\u0002\n\u0002\u0018\u0002\n\u0000\n\u0002\u0018\u0002\n\u0000\n\u0002\u0018\u0002\n\u0000\n\u0002\u0010\u0002\n\u0000\n\u0002\u0010\u000e\n\u0002\u0008\u0002\n\u0002\u0010\t\n\u0002\u0008\u0004\u0018\u00002\u00020\u0001B\u0005\u00a2\u0006\u0002\u0010\u0002J\u0006\u0010\u000e\u001a\u00020\u000fJ\u0018\u0010\u0010\u001a\u00020\u00112\u0006\u0010\u0012\u001a\u00020\u00112\u0008\u0008\u0002\u0010\u0013\u001a\u00020\u0014J\u0018\u0010\u0015\u001a\u00020\u00112\u0006\u0010\u0012\u001a\u00020\u00112\u0006\u0010\u0013\u001a\u00020\u0014H\u0002J\u0006\u0010\u0016\u001a\u00020\u0004J\u0006\u0010\u0017\u001a\u00020\u0004R\u000e\u0010\u0003\u001a\u00020\u0004X\u0082\u000e\u00a2\u0006\u0002\n\u0000R\u0016\u0010\u0005\u001a\n \u0007*\u0004\u0018\u00010\u00060\u0006X\u0082\u0004\u00a2\u0006\u0002\n\u0000R\u0010\u0010\u0008\u001a\u0004\u0018\u00010\tX\u0082\u000e\u00a2\u0006\u0002\n\u0000R\u0010\u0010\n\u001a\u0004\u0018\u00010\u000bX\u0082\u000e\u00a2\u0006\u0002\n\u0000R\u0010\u0010\u000c\u001a\u0004\u0018\u00010\rX\u0082\u000e\u00a2\u0006\u0002\n\u0000\u00a8\u0006\u0018"
    }
    d2 = {
        "Lcom/dbdm/switcher/RootShell;",
        "",
        "()V",
        "alive",
        "",
        "executor",
        "Ljava/util/concurrent/ExecutorService;",
        "kotlin.jvm.PlatformType",
        "process",
        "Ljava/lang/Process;",
        "reader",
        "Ljava/io/BufferedReader;",
        "writer",
        "Ljava/io/OutputStreamWriter;",
        "close",
        "",
        "exec",
        "",
        "cmd",
        "timeoutMs",
        "",
        "execInternal",
        "isAlive",
        "open",
        "app_debug"
    }
    k = 0x1
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation


# instance fields
.field private alive:Z

.field private final executor:Ljava/util/concurrent/ExecutorService;

.field private process:Ljava/lang/Process;

.field private reader:Ljava/io/BufferedReader;

.field private writer:Ljava/io/OutputStreamWriter;


# direct methods
.method public static synthetic $r8$lambda$wYJ8HJOI1muU2SR8Jv3t2qrt4Og(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;)Ljava/lang/String;
    .locals 0

    invoke-static {p0, p1}, Lcom/dbdm/switcher/RootShell;->execInternal$lambda$1(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;)Ljava/lang/String;

    move-result-object p0

    return-object p0
.end method

.method public constructor <init>()V
    .locals 1

    .line 17
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 23
    invoke-static {}, Ljava/util/concurrent/Executors;->newSingleThreadExecutor()Ljava/util/concurrent/ExecutorService;

    move-result-object v0

    iput-object v0, p0, Lcom/dbdm/switcher/RootShell;->executor:Ljava/util/concurrent/ExecutorService;

    .line 17
    return-void
.end method

.method public static synthetic exec$default(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;JILjava/lang/Object;)Ljava/lang/String;
    .locals 0

    .line 46
    and-int/lit8 p4, p4, 0x2

    if-eqz p4, :cond_0

    const-wide/16 p2, 0x7530

    :cond_0
    invoke-virtual {p0, p1, p2, p3}, Lcom/dbdm/switcher/RootShell;->exec(Ljava/lang/String;J)Ljava/lang/String;

    move-result-object p0

    return-object p0
.end method

.method private final execInternal(Ljava/lang/String;J)Ljava/lang/String;
    .locals 6
    .param p1, "cmd"    # Ljava/lang/String;
    .param p2, "timeoutMs"    # J

    .line 52
    const-string v0, "]"

    invoke-static {}, Ljava/lang/System;->nanoTime()J

    move-result-wide v1

    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3}, Ljava/lang/StringBuilder;-><init>()V

    const-string v4, "___DONE_"

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v1, v2}, Ljava/lang/StringBuilder;->append(J)Ljava/lang/StringBuilder;

    move-result-object v1

    const-string v2, "___"

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    .line 53
    .local v1, "sentinel":Ljava/lang/String;
    nop

    .line 54
    :try_start_0
    iget-object v2, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_1

    const-string v3, "\n"

    if-eqz v2, :cond_0

    :try_start_1
    new-instance v4, Ljava/lang/StringBuilder;

    invoke-direct {v4}, Ljava/lang/StringBuilder;-><init>()V

    invoke-virtual {v4, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v4

    invoke-virtual {v2, v4}, Ljava/io/OutputStreamWriter;->write(Ljava/lang/String;)V

    .line 55
    :cond_0
    iget-object v2, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;

    if-eqz v2, :cond_1

    new-instance v4, Ljava/lang/StringBuilder;

    invoke-direct {v4}, Ljava/lang/StringBuilder;-><init>()V

    const-string v5, "\necho "

    invoke-virtual {v4, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v3

    invoke-virtual {v2, v3}, Ljava/io/OutputStreamWriter;->write(Ljava/lang/String;)V

    .line 56
    :cond_1
    iget-object v2, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;

    if-eqz v2, :cond_2

    invoke-virtual {v2}, Ljava/io/OutputStreamWriter;->flush()V

    .line 58
    :cond_2
    iget-object v2, p0, Lcom/dbdm/switcher/RootShell;->executor:Ljava/util/concurrent/ExecutorService;

    new-instance v3, Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;

    invoke-direct {v3, p0, v1}, Lcom/dbdm/switcher/RootShell$$ExternalSyntheticLambda0;-><init>(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;)V

    invoke-interface {v2, v3}, Ljava/util/concurrent/ExecutorService;->submit(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;

    move-result-object v2
    :try_end_1
    .catch Ljava/lang/Throwable; {:try_start_1 .. :try_end_1} :catch_1

    .line 68
    .local v2, "future":Ljava/util/concurrent/Future;
    nop

    .line 69
    :try_start_2
    sget-object v3, Ljava/util/concurrent/TimeUnit;->MILLISECONDS:Ljava/util/concurrent/TimeUnit;

    invoke-interface {v2, p2, p3, v3}, Ljava/util/concurrent/Future;->get(JLjava/util/concurrent/TimeUnit;)Ljava/lang/Object;

    move-result-object v3

    check-cast v3, Ljava/lang/String;
    :try_end_2
    .catch Ljava/util/concurrent/TimeoutException; {:try_start_2 .. :try_end_2} :catch_0
    .catch Ljava/lang/Throwable; {:try_start_2 .. :try_end_2} :catch_1

    goto :goto_0

    .line 70
    :catch_0
    move-exception v3

    .line 71
    .local v3, "e":Ljava/util/concurrent/TimeoutException;
    const/4 v4, 0x1

    :try_start_3
    invoke-interface {v2, v4}, Ljava/util/concurrent/Future;->cancel(Z)Z

    .line 72
    new-instance v4, Ljava/lang/StringBuilder;

    invoke-direct {v4}, Ljava/lang/StringBuilder;-><init>()V

    const-string v5, "[TIMEOUT after "

    invoke-virtual {v4, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, p2, p3}, Ljava/lang/StringBuilder;->append(J)Ljava/lang/StringBuilder;

    move-result-object v4

    const-string v5, "ms: "

    invoke-virtual {v4, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v4

    move-object v3, v4

    .end local v3    # "e":Ljava/util/concurrent/TimeoutException;
    :goto_0
    nop

    .line 53
    .end local v2    # "future":Ljava/util/concurrent/Future;
    nop

    .line 71
    invoke-static {v3}, Lkotlin/jvm/internal/Intrinsics;->checkNotNull(Ljava/lang/Object;)V
    :try_end_3
    .catch Ljava/lang/Throwable; {:try_start_3 .. :try_end_3} :catch_1

    goto :goto_1

    .line 74
    :catch_1
    move-exception v2

    .line 75
    .local v2, "e":Ljava/lang/Throwable;
    const/4 v3, 0x0

    iput-boolean v3, p0, Lcom/dbdm/switcher/RootShell;->alive:Z

    .line 76
    invoke-virtual {v2}, Ljava/lang/Throwable;->getMessage()Ljava/lang/String;

    move-result-object v3

    new-instance v4, Ljava/lang/StringBuilder;

    invoke-direct {v4}, Ljava/lang/StringBuilder;-><init>()V

    const-string v5, "[ERROR: "

    invoke-virtual {v4, v5}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v4

    invoke-virtual {v4, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v3

    .line 53
    .end local v2    # "e":Ljava/lang/Throwable;
    :goto_1
    return-object v3
.end method

.method private static final execInternal$lambda$1(Lcom/dbdm/switcher/RootShell;Ljava/lang/String;)Ljava/lang/String;
    .locals 5
    .param p0, "this$0"    # Lcom/dbdm/switcher/RootShell;
    .param p1, "$sentinel"    # Ljava/lang/String;

    const-string v0, "this$0"

    invoke-static {p0, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    const-string v0, "$sentinel"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 59
    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V

    .line 60
    .local v0, "sb":Ljava/lang/StringBuilder;
    :try_start_0
    const/4 v1, 0x0

    .line 61
    .local v1, "line":Ljava/lang/Object;
    :goto_0
    iget-object v2, p0, Lcom/dbdm/switcher/RootShell;->reader:Ljava/io/BufferedReader;

    if-eqz v2, :cond_0

    invoke-virtual {v2}, Ljava/io/BufferedReader;->readLine()Ljava/lang/String;

    move-result-object v2

    goto :goto_1

    :cond_0
    const/4 v2, 0x0

    :goto_1
    move-object v3, v2

    .line 93
    .local v3, "it":Ljava/lang/String;
    const/4 v4, 0x0

    .line 61
    .local v4, "$i$a$-also-RootShell$execInternal$future$1$1":I
    move-object v1, v3

    .end local v3    # "it":Ljava/lang/String;
    .end local v4    # "$i$a$-also-RootShell$execInternal$future$1$1":I
    if-eqz v2, :cond_1

    .line 62
    move-object v3, p1

    check-cast v3, Ljava/lang/CharSequence;

    invoke-virtual {v1, v3}, Ljava/lang/String;->contains(Ljava/lang/CharSequence;)Z

    move-result v2

    if-nez v2, :cond_1

    .line 63
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    const-string v3, "append(...)"

    invoke-static {v2, v3}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    const/16 v4, 0xa

    invoke-virtual {v2, v4}, Ljava/lang/StringBuilder;->append(C)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-static {v2, v3}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    goto :goto_0

    :catch_0
    move-exception v2

    .line 65
    :cond_1
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    const-string v3, "toString(...)"

    invoke-static {v2, v3}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    check-cast v2, Ljava/lang/CharSequence;

    invoke-static {v2}, Lkotlin/text/StringsKt;->trimEnd(Ljava/lang/CharSequence;)Ljava/lang/CharSequence;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/Object;->toString()Ljava/lang/String;

    move-result-object v2

    return-object v2
.end method


# virtual methods
.method public final close()V
    .locals 2

    .line 83
    const/4 v0, 0x0

    iput-boolean v0, p0, Lcom/dbdm/switcher/RootShell;->alive:Z

    .line 84
    :try_start_0
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;

    if-eqz v0, :cond_0

    const-string v1, "exit\n"

    invoke-virtual {v0, v1}, Ljava/io/OutputStreamWriter;->write(Ljava/lang/String;)V

    :cond_0
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;

    if-eqz v0, :cond_1

    invoke-virtual {v0}, Ljava/io/OutputStreamWriter;->flush()V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    goto :goto_0

    :catch_0
    move-exception v0

    .line 85
    :cond_1
    :goto_0
    :try_start_1
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->process:Ljava/lang/Process;

    if-eqz v0, :cond_2

    invoke-virtual {v0}, Ljava/lang/Process;->waitFor()I
    :try_end_1
    .catch Ljava/lang/Throwable; {:try_start_1 .. :try_end_1} :catch_1

    goto :goto_1

    :catch_1
    move-exception v0

    .line 86
    :cond_2
    :goto_1
    :try_start_2
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;

    if-eqz v0, :cond_3

    invoke-virtual {v0}, Ljava/io/OutputStreamWriter;->close()V
    :try_end_2
    .catch Ljava/lang/Throwable; {:try_start_2 .. :try_end_2} :catch_2

    goto :goto_2

    :catch_2
    move-exception v0

    .line 87
    :cond_3
    :goto_2
    :try_start_3
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->reader:Ljava/io/BufferedReader;

    if-eqz v0, :cond_4

    invoke-virtual {v0}, Ljava/io/BufferedReader;->close()V
    :try_end_3
    .catch Ljava/lang/Throwable; {:try_start_3 .. :try_end_3} :catch_3

    goto :goto_3

    :catch_3
    move-exception v0

    .line 88
    :cond_4
    :goto_3
    :try_start_4
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->process:Ljava/lang/Process;

    if-eqz v0, :cond_5

    invoke-virtual {v0}, Ljava/lang/Process;->destroy()V
    :try_end_4
    .catch Ljava/lang/Throwable; {:try_start_4 .. :try_end_4} :catch_4

    goto :goto_4

    :catch_4
    move-exception v0

    .line 89
    :cond_5
    :goto_4
    :try_start_5
    iget-object v0, p0, Lcom/dbdm/switcher/RootShell;->executor:Ljava/util/concurrent/ExecutorService;

    invoke-interface {v0}, Ljava/util/concurrent/ExecutorService;->shutdownNow()Ljava/util/List;
    :try_end_5
    .catch Ljava/lang/Throwable; {:try_start_5 .. :try_end_5} :catch_5

    goto :goto_5

    :catch_5
    move-exception v0

    .line 90
    :goto_5
    return-void
.end method

.method public final declared-synchronized exec(Ljava/lang/String;J)Ljava/lang/String;
    .locals 1
    .param p1, "cmd"    # Ljava/lang/String;
    .param p2, "timeoutMs"    # J

    monitor-enter p0

    :try_start_0
    const-string v0, "cmd"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 47
    iget-boolean v0, p0, Lcom/dbdm/switcher/RootShell;->alive:Z

    if-nez v0, :cond_0

    const-string v0, "echo __ROOT_OK__"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->areEqual(Ljava/lang/Object;Ljava/lang/Object;)Z

    move-result v0

    if-nez v0, :cond_0

    invoke-virtual {p0}, Lcom/dbdm/switcher/RootShell;->open()Z

    move-result v0

    if-nez v0, :cond_0

    const-string v0, ""
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    monitor-exit p0

    return-object v0

    .line 48
    :cond_0
    :try_start_1
    invoke-direct {p0, p1, p2, p3}, Lcom/dbdm/switcher/RootShell;->execInternal(Ljava/lang/String;J)Ljava/lang/String;

    move-result-object v0
    :try_end_1
    .catchall {:try_start_1 .. :try_end_1} :catchall_0

    monitor-exit p0

    return-object v0

    :catchall_0
    move-exception p1

    monitor-exit p0

    throw p1
.end method

.method public final isAlive()Z
    .locals 1

    .line 80
    iget-boolean v0, p0, Lcom/dbdm/switcher/RootShell;->alive:Z

    return v0
.end method

.method public final open()Z
    .locals 6

    .line 27
    nop

    .line 28
    const/4 v0, 0x0

    :try_start_0
    iget-object v1, p0, Lcom/dbdm/switcher/RootShell;->process:Ljava/lang/Process;

    if-eqz v1, :cond_0

    invoke-virtual {v1}, Ljava/lang/Process;->destroy()V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_init

    goto :cond_0

    :catch_init
    move-exception v1

    :cond_0
    :try_start_1
    invoke-static {}, Ljava/lang/Runtime;->getRuntime()Ljava/lang/Runtime;

    move-result-object v1

    const-string v2, "su"

    invoke-virtual {v1, v2}, Ljava/lang/Runtime;->exec(Ljava/lang/String;)Ljava/lang/Process;

    move-result-object v1

    iput-object v1, p0, Lcom/dbdm/switcher/RootShell;->process:Ljava/lang/Process;

    .line 29
    new-instance v1, Ljava/io/OutputStreamWriter;

    iget-object v2, p0, Lcom/dbdm/switcher/RootShell;->process:Ljava/lang/Process;

    invoke-static {v2}, Lkotlin/jvm/internal/Intrinsics;->checkNotNull(Ljava/lang/Object;)V

    invoke-virtual {v2}, Ljava/lang/Process;->getOutputStream()Ljava/io/OutputStream;

    move-result-object v2

    sget-object v3, Lkotlin/text/Charsets;->UTF_8:Ljava/nio/charset/Charset;

    invoke-direct {v1, v2, v3}, Ljava/io/OutputStreamWriter;-><init>(Ljava/io/OutputStream;Ljava/nio/charset/Charset;)V

    iput-object v1, p0, Lcom/dbdm/switcher/RootShell;->writer:Ljava/io/OutputStreamWriter;

    .line 30
    new-instance v1, Ljava/io/BufferedReader;

    new-instance v2, Ljava/io/InputStreamReader;

    iget-object v3, p0, Lcom/dbdm/switcher/RootShell;->process:Ljava/lang/Process;

    invoke-static {v3}, Lkotlin/jvm/internal/Intrinsics;->checkNotNull(Ljava/lang/Object;)V

    invoke-virtual {v3}, Ljava/lang/Process;->getInputStream()Ljava/io/InputStream;

    move-result-object v3

    sget-object v4, Lkotlin/text/Charsets;->UTF_8:Ljava/nio/charset/Charset;

    invoke-direct {v2, v3, v4}, Ljava/io/InputStreamReader;-><init>(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V

    check-cast v2, Ljava/io/Reader;

    invoke-direct {v1, v2}, Ljava/io/BufferedReader;-><init>(Ljava/io/Reader;)V

    iput-object v1, p0, Lcom/dbdm/switcher/RootShell;->reader:Ljava/io/BufferedReader;

    .line 32
    const-string v1, "export PATH=/system/bin:/system/xbin:/sbin:/vendor/bin:$PATH; echo __ROOT_OK__"

    const-wide/16 v2, 0x7530

    invoke-direct {p0, v1, v2, v3}, Lcom/dbdm/switcher/RootShell;->execInternal(Ljava/lang/String;J)Ljava/lang/String;

    move-result-object v1

    .line 33
    .local v1, "probe":Ljava/lang/String;
    move-object v2, v1

    check-cast v2, Ljava/lang/CharSequence;

    const-string v3, "__ROOT_OK__"

    check-cast v3, Ljava/lang/CharSequence;

    const/4 v4, 0x2

    const/4 v5, 0x0

    invoke-static {v2, v3, v0, v4, v5}, Lkotlin/text/StringsKt;->contains$default(Ljava/lang/CharSequence;Ljava/lang/CharSequence;ZILjava/lang/Object;)Z

    move-result v2

    iput-boolean v2, p0, Lcom/dbdm/switcher/RootShell;->alive:Z

    .line 34
    iget-boolean v0, p0, Lcom/dbdm/switcher/RootShell;->alive:Z
    :try_end_1
    .catch Ljava/lang/Throwable; {:try_start_1 .. :try_end_1} :catch_0

    .end local v1    # "probe":Ljava/lang/String;
    goto :goto_0

    .line 35
    :catch_0
    move-exception v1

    .line 36
    .local v1, "e":Ljava/lang/Throwable;
    iput-boolean v0, p0, Lcom/dbdm/switcher/RootShell;->alive:Z

    .line 37
    nop

    .line 27
    .end local v1    # "e":Ljava/lang/Throwable;
    :goto_0
    return v0
.end method
