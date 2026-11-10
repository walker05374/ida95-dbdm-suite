.class final Lcom/dbdm/switcher/MainActivity$initRoot$1$1;
.super Lkotlin/coroutines/jvm/internal/SuspendLambda;
.source "MainActivity.kt"

# interfaces
.implements Lkotlin/jvm/functions/Function2;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/dbdm/switcher/MainActivity$initRoot$1;->invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;
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
    c = "com.dbdm.switcher.MainActivity$initRoot$1$1"
    f = "MainActivity.kt"
    i = {}
    l = {}
    m = "invokeSuspend"
    n = {}
    s = {}
.end annotation


# instance fields
.field final synthetic $detectedUserId:I

.field final synthetic $detectedVm:Z

.field final synthetic $ok:Z

.field label:I

.field final synthetic this$0:Lcom/dbdm/switcher/MainActivity;


# direct methods
.method constructor <init>(ZLcom/dbdm/switcher/MainActivity;ZILkotlin/coroutines/Continuation;)V
    .locals 1
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(Z",
            "Lcom/dbdm/switcher/MainActivity;",
            "ZI",
            "Lkotlin/coroutines/Continuation<",
            "-",
            "Lcom/dbdm/switcher/MainActivity$initRoot$1$1;",
            ">;)V"
        }
    .end annotation

    iput-boolean p1, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$ok:Z

    iput-object p2, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iput-boolean p3, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$detectedVm:Z

    iput p4, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$detectedUserId:I

    const/4 v0, 0x2

    invoke-direct {p0, v0, p5}, Lkotlin/coroutines/jvm/internal/SuspendLambda;-><init>(ILkotlin/coroutines/Continuation;)V

    return-void
.end method


# virtual methods
.method public final create(Ljava/lang/Object;Lkotlin/coroutines/Continuation;)Lkotlin/coroutines/Continuation;
    .locals 7
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

    new-instance v6, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;

    iget-boolean v1, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$ok:Z

    iget-object v2, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iget-boolean v3, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$detectedVm:Z

    iget v4, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$detectedUserId:I

    move-object v0, v6

    move-object v5, p2

    invoke-direct/range {v0 .. v5}, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;-><init>(ZLcom/dbdm/switcher/MainActivity;ZILkotlin/coroutines/Continuation;)V

    check-cast v6, Lkotlin/coroutines/Continuation;

    return-object v6
.end method

.method public bridge synthetic invoke(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;
    .locals 1

    check-cast p1, Lkotlinx/coroutines/CoroutineScope;

    check-cast p2, Lkotlin/coroutines/Continuation;

    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->invoke(Lkotlinx/coroutines/CoroutineScope;Lkotlin/coroutines/Continuation;)Ljava/lang/Object;

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

    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->create(Ljava/lang/Object;Lkotlin/coroutines/Continuation;)Lkotlin/coroutines/Continuation;

    move-result-object v0

    check-cast v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;

    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    invoke-virtual {v0, v1}, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    return-object v0
.end method

.method public final invokeSuspend(Ljava/lang/Object;)Ljava/lang/Object;
    .locals 4

    invoke-static {}, Lkotlin/coroutines/intrinsics/IntrinsicsKt;->getCOROUTINE_SUSPENDED()Ljava/lang/Object;

    .line 79
    iget v0, p0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->label:I

    packed-switch v0, :pswitch_data_0

    new-instance p1, Ljava/lang/IllegalStateException;

    const-string v0, "call to \'resume\' before \'invoke\' with coroutine"

    invoke-direct {p1, v0}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw p1

    :pswitch_0
    invoke-static {p1}, Lkotlin/ResultKt;->throwOnFailure(Ljava/lang/Object;)V

    move-object v0, p0

    .line 80
    .local v0, "this":Lcom/dbdm/switcher/MainActivity$initRoot$1$1;
    .local p1, "$result":Ljava/lang/Object;
    iget-boolean v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$ok:Z

    if-nez v1, :cond_0

    .line 81
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    const/4 v2, 0x0

    const-string v3, "\u274c Root n\u00e3o concedido"

    invoke-static {v1, v2, v3}, Lcom/dbdm/switcher/MainActivity;->access$setUi(Lcom/dbdm/switcher/MainActivity;ZLjava/lang/String;)V

    .line 82
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    const-string v2, "Conceda root ao app e reabra"

    invoke-static {v1, v2}, Lcom/dbdm/switcher/MainActivity;->access$toast(Lcom/dbdm/switcher/MainActivity;Ljava/lang/String;)V

    .line 83
    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    return-object v1

    .line 85
    :cond_0
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iget-boolean v2, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$detectedVm:Z

    invoke-static {v1, v2}, Lcom/dbdm/switcher/MainActivity;->access$setVmMode$p(Lcom/dbdm/switcher/MainActivity;Z)V

    .line 86
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    iget v2, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->$detectedUserId:I

    invoke-static {v1, v2}, Lcom/dbdm/switcher/MainActivity;->access$setGameUserId$p(Lcom/dbdm/switcher/MainActivity;I)V

    .line 87
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v1}, Lcom/dbdm/switcher/MainActivity;->access$updateModeUi(Lcom/dbdm/switcher/MainActivity;)V

    .line 88
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v1}, Lcom/dbdm/switcher/MainActivity;->access$getGameUserId$p(Lcom/dbdm/switcher/MainActivity;)I

    move-result v1

    const/16 v2, 0x96

    if-ne v1, v2, :cond_1

    .line 89
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    const-string v2, "Virtual Master detectado \u2014 user 150"

    invoke-static {v1, v2}, Lcom/dbdm/switcher/MainActivity;->access$toast(Lcom/dbdm/switcher/MainActivity;Ljava/lang/String;)V

    .line 91
    :cond_1
    iget-object v1, v0, Lcom/dbdm/switcher/MainActivity$initRoot$1$1;->this$0:Lcom/dbdm/switcher/MainActivity;

    invoke-static {v1}, Lcom/dbdm/switcher/MainActivity;->access$refresh(Lcom/dbdm/switcher/MainActivity;)V

    .line 92
    sget-object v1, Lkotlin/Unit;->INSTANCE:Lkotlin/Unit;

    return-object v1

    :pswitch_data_0
    .packed-switch 0x0
        :pswitch_0
    .end packed-switch
.end method
