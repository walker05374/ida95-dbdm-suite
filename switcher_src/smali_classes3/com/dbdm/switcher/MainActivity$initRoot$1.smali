.class final Lcom/dbdm/switcher/MainActivity$initRoot$1;
.super Lkotlin/coroutines/jvm/internal/SuspendLambda;
.source "MainActivity.kt"

# interfaces
.implements Lkotlin/jvm/functions/Function2;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/dbdm/switcher/MainActivity;->initRoot()V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x18
    name = null
.end annotation

.annotation system Ldalvik/annotation/Signature;
    value = {
        "Lkotlin/coroutines/jvm/internal/SuspendLambda;",
        "Lkotlin/jvm/functions/Function2<",
        "Lkotlinx/coroutines/CoroutineScope;",
        "Lkotlin/coroutines/Continuation<",
        "-",
        "Lkotlin/Unit;",
        ">;",
        "Ljava/lang/Object;",
        ">;"
    }
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000\n\n\u0000\n\u0002\u0010\u0002\n\u0002\u0018\u0002\u0010\u0000\u001a\u00020\u0001*\u00020\u0002H\u008a@"
    }
    d2 = {
        "<anonymous>",
        "",
        "Lkotlinx/coroutines/CoroutineScope;"
    }
    k = 0x3
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation

.annotation runtime Lkotlin/coroutines/jvm/internal/DebugMetadata;
    c = "com.dbdm.switcher.MainActivity$initRoot$1"
    f = "MainActivity.kt"
    i = {}
    l = {
        0x4f
    }
    m = "invokeSuspend"
    n = {}
    s = {}
.end annotation


# instance fields
.field label:I

.field final synthetic this$0:Lcom/dbdm/switcher/MainActivity;


# direct methods
.method constructor <init>(Lcom/dbdm/switcher/MainActivity;Lkotlin/coroutines/Continuation;)V
    .locals 1
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Lcom/dbdm/switcher/MainActivity;",
            "Lkotlin/coroutines/Continuation<",
            "-",
            "Lcom/dbdm/switcher/MainActivity$initRoot$1;",
            ">;)V"
        }
    .end annotation

    iput-object p1, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    const/4 v0, 0x2

    invoke-direct {p0, v0, p2}, Lkotlin/coroutines/jvm/internal/SuspendLambda;-><init>(ILkotlin/coroutines/Continuation;)V

    return-void
.end method


# virtual methods
.method public final create(Ljava/lang/Object;Lkotlin/coroutines/Continuation;)Lkotlin/coroutines/Continuation;
    .locals 2
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Ljava/lang/Object;",
            "Lkotlin/coroutines/Continuation<",
            "*>;)",
            "Lkotlin/coroutines/Continuation<",
            "Lkotlin/Unit;",
            ">;"
        }
    .end annotation

    new-instance v0, Lcom/dbdm/switcher/MainActivity$initRoot$1;

    iget-object v1, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-direct {v0, v1, p2}, Lcom/dbdm/switcher/MainActivity$initRoot$1;-><init>(Lcom/dbdm/switcher/MainActivity;Lkotlin/coroutines/Continuation;)V

    check-cast v0, Lkotlin/coroutines/Continuation;

    return-object v0
.end method

.method public bridge synthetic invoke(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;
    .locals 1

    check-cast p1, Lkotlinx/coroutines/CoroutineScope;

    check-cast p2, Lkotlin/coroutines/Continuation;

    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/MainActivity$initRoot$1;->invoke(Lkotlinx/coroutines/CoroutineScope;Lkotlin/coroutines/Continuation;)Ljava/lang/Object;

    move-result-object v0

    return-object v0
.end method

.method public final invoke(Lkotlinx/coroutines/CoroutineScope;Lkotlin/coroutines/Continuation;)Ljava/lang/Object;
    .locals 2
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Lkotlinx/coroutines/CoroutineScope;",
            "Lkotlin/coroutines/Continuation<",
            "-",
            "Lkotlin/Unit;",
            ">;)",
            "Ljava/lang/Object;"
        }
    .end annotation

    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/MainActivity$initRoot$1;->create(Ljava/lang/Object;Lkotlin/coroutines/Continuation;)Lkotlin/coroutines/Continuation;

    move-result-object v0

    check-cast v0, Lcom/dbdm/switcher/MainActivity$initRoot$1;

    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    invoke-virtual {v0, v1}, Lcom/dbdm/switcher/MainActivity$initRoot$1;->invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    return-object v0
.end method

.method public final invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;
    .locals 14

    invoke-static {}, Lkotlin/coroutines/intrinsics/IntrinsicsKt;->getCOROUTINE_SUSPENDED()Ljava/lang/Object;

    move-result-object v0

    .line 75
    iget v1, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1;->label:I

    packed-switch v1, :pswitch_data_0

    new-instance p1, Ljava/lang/IllegalStateException;

    const-string v0, "call to \'resume\' before \'invoke\' with coroutine"

    invoke-direct {p1, v0}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw p1

    :pswitch_0
    move-object v0, p0

    .local v0, "this":Lcom/dbdm/switcher/MainActivity$initRoot$1;
    .local p1, "$result":Ljava/lang/Object;
    invoke-static {p1}, Lkotlin/ResultKt;->throwOnFailure(Ljava/lang/Object;)V

    goto :goto_4

    .end local v0    # "this":Lcom/dbdm/switcher/MainActivity$initRoot$1;
    .end local p1    # "$result":Ljava/lang/Object;
    :pswitch_1
    invoke-static {p1}, Lkotlin/ResultKt;->throwOnFailure(Ljava/lang/Object;)V

    move-object v1, p0

    .line 76
    .local v1, "this":Lcom/dbdm/switcher/MainActivity$initRoot$1;
    .restart local p1    # "$result":Ljava/lang/Object;
    iget-object v2, v1, Lcom/dbdm/switcher/MainActivity$initRoot$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v2}, Lcom/dbdm/switcher/MainActivity;->access$getShell$p(Lcom/dbdm/switcher/MainActivity;)Lcom/dbdm/switcher/RootShell;

    move-result-object v2

    invoke-virtual {v2}, Lcom/dbdm/switcher/RootShell;->open()Z

    move-result v2

    .line 77
    .local v2, "ok":Z
    const/4 v3, 0x0

    if-eqz v2, :cond_0

    sget-object v4, Lcom/dbdm/switcher/SsaidManager;->INSTANCE:Lcom/dbdm/switcher/SsaidManager;

    iget-object v5, v1, Lcom/dbdm/switcher/MainActivity$initRoot$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v5}, Lcom/dbdm/switcher/MainActivity;->access$getShell$p(Lcom/dbdm/switcher/MainActivity;)Lcom/dbdm/switcher/RootShell;

    move-result-object v5

    invoke-virtual {v4, v5}, Lcom/dbdm/switcher/SsaidManager;->isVirtualMachine(Lcom/dbdm/switcher/RootShell;)Z

    move-result v4

    goto :goto_0

    :cond_0
    move v4, v3

    .line 78
    .local v4, "detectedVm":Z
    :goto_0
    if-eqz v2, :cond_1

    sget-object v5, Lcom/dbdm/switcher/SsaidManager;->INSTANCE:Lcom/dbdm/switcher/SsaidManager;

    iget-object v6, v1, Lcom/dbdm/switcher/MainActivity$initRoot$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v6}, Lcom/dbdm/switcher/MainActivity;->access$getShell$p(Lcom/dbdm/switcher/MainActivity;)Lcom/dbdm/switcher/RootShell;

    move-result-object v6

    invoke-virtual {v5, v6}, Lcom/dbdm/switcher/SsaidManager;->detectGameUserId(Lcom/dbdm/switcher/RootShell;)I

    move-result v5

    move v10, v5

    goto :goto_1

    :cond_1
    move v10, v3

    .line 79
    .local v10, "detectedUserId":I
    :goto_1
    invoke-static {}, Lkotlinx/coroutines/Dispatchers;->getMain()Lkotlinx/coroutines/MainCoroutineDispatcher;

    move-result-object v5

    check-cast v5, Lkotlin/coroutines/CoroutineContext;

    new-instance v12, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;

    const/4 v13, 0x1

    if-eqz v2, :cond_2

    move v7, v13

    goto :goto_2

    :cond_2
    move v7, v3

    .end local v2    # "ok":Z
    :goto_2
    iget-object v8, v1, Lcom/dbdm/switcher/MainActivity$initRoot$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    if-eqz v4, :cond_3

    move v9, v13

    goto :goto_3

    :cond_3
    move v9, v3

    .end local v4    # "detectedVm":Z
    :goto_3
    const/4 v11, 0x0

    move-object v6, v12

    invoke-direct/range {v6 .. v11}, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;-><init>(ZLcom/dbdm/switcher/MainActivity;ZILkotlin/coroutines/Continuation;)V

    check-cast v12, Lkotlin/jvm/functions/Function2;

    move-object v2, v1

    check-cast v2, Lkotlin/coroutines/Continuation;

    iput v13, v1, Lcom/dbdm/switcher/MainActivity$initRoot$1;->label:I

    invoke-static {v5, v12, v2}, Lkotlinx/coroutines/BuildersKt;->withContext(Lkotlin/coroutines/CoroutineContext;Lkotlin/jvm/functions/Function2;Lkotlin/coroutines/Continuation;)Ljava/lang/Object;

    move-result-object v2

    .end local v10    # "detectedUserId":I
    if-ne v2, v0, :cond_4

    .line 75
    return-object v0

    .line 79
    :cond_4
    move-object v0, v1

    .line 93
    .end local v1    # "this":Lcom/dbdm/switcher/MainActivity$initRoot$1;
    .restart local v0    # "this":Lcom/dbdm/switcher/MainActivity$initRoot$1;
    :goto_4
    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    return-object v1

    :pswitch_data_0
    .packed-switch 0x0
        :pswitch_1
        :pswitch_0
    .end packed-switch
.end method
